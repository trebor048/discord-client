#include "MessageInput.hpp"
#include "AttachmentPreviewPanel.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include "Core/EmojiCatalog.hpp"
#include "Core/Theme/Manager.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Entities.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "UI/Dialogs/GifPickerDialog.hpp"
#include "UI/Dialogs/StickerPickerDialog.hpp"
#include "UI/Widgets/Chat/EmojiAutocompletePopup.hpp"
#include "UI/Widgets/Chat/SlashCommandPopup.hpp"
#include "UI/Widgets/Chat/MentionAutocompletePopup.hpp"
#include "Core/Theme/Icons.hpp"
#include <QBuffer>
#include <QVBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QImageReader>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QFrame>
#include <QImage>
#include <QMimeData>
#include <QRegularExpression>
#include <QScrollBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QScreen>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {

// Forward decls (defined below, used by the returnPressed handler).
bool hasAllRequiredOptions(const Discord::ApplicationCommand &command,
                           const QList<Discord::InteractionOptionValue> &parsed);

QString pickEmoji(QWidget *parent, const QString &title, const QString &prompt,
                  const QStringList &orderedGuildIds = {},
                  const Core::Snowflake &currentGuildId = {})
{
    EmojiPickerDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setSearchPlaceholder(prompt);
    if (!orderedGuildIds.isEmpty())
        dialog.setOrderedGuildIds(orderedGuildIds);
    if (currentGuildId.isValid())
        dialog.setCurrentGuildId(currentGuildId.toString());

    if (dialog.exec() != QDialog::Accepted)
        return {};
    return dialog.selectedEmoji();
}

QString markdownPreviewHtml(const QString &body)
{
    return QStringLiteral(
                   "<html><head><style>"
                   "body { margin: 0; color: #dbdee1; font-size: 13px; }"
                   "a { color: #00a8fc; text-decoration: none; }"
                   ".mention { color: #c9cdfb; background: rgba(88, 101, 242, 0.25); "
                   "border-radius: 3px; padding: 0 2px; }"
                   "code { background: rgba(0, 0, 0, 0.25); border-radius: 3px; padding: 1px 3px; }"
                   "</style></head><body>%1</body></html>")
            .arg(body);
}

// Sanitize HTML to prevent XSS in the markdown preview.
// Strips dangerous constructs even though the parser should already produce
// safe output (defense-in-depth).
static QString sanitizeHtml(const QString &html)
{
    QString result = html;
    // Remove <script>...</script> blocks (including variants with attributes)
    static const QRegularExpression scriptRe(
        QStringLiteral("<script[^>]*>[\\s\\S]*?</script>"),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(scriptRe);
    // Remove self-closing <script ... /> (rare but possible)
    static const QRegularExpression scriptSelfClosRe(
        QStringLiteral("<script[^>]*/>"),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(scriptSelfClosRe);
    // Remove <iframe>...</iframe> blocks
    static const QRegularExpression iframeRe(
        QStringLiteral("<iframe[^>]*>[\\s\\S]*?</iframe>"),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(iframeRe);
    static const QRegularExpression iframeSelfClosRe(
        QStringLiteral("<iframe[^>]*/>"),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(iframeSelfClosRe);
    // Remove JavaScript pseudo-protocol in href/src attributes
    static const QRegularExpression jsInHref(
        QStringLiteral("(href|src)\\s*=\\s*[\"']\\s*javascript\\s*:"),
        QRegularExpression::CaseInsensitiveOption);
    result.replace(jsInHref, QStringLiteral("href=\"about:blank\""));
    // Remove event handler attributes (onclick, onload, onerror, etc.)
    static const QRegularExpression eventHandler(
        QStringLiteral("\\son\\w+\\s*=\\s*\"[^\"]*\""),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(eventHandler);
    static const QRegularExpression eventHandlerSingle(
        QStringLiteral("\\son\\w+\\s*=\\s*'[^']*'"),
        QRegularExpression::CaseInsensitiveOption);
    result.remove(eventHandlerSingle);
    return result;
}

QString gifFilenamePrefix(const Discord::GifItem &gif)
{
    const auto providerHost = [](const QUrl &url) {
        return url.host().toLower();
    };

    const QString shareHost = providerHost(gif.url);
    const QString fullHost = providerHost(gif.full.url);
    const QString previewHost = providerHost(gif.preview.url);
    const QString host = !shareHost.isEmpty() ? shareHost : (!fullHost.isEmpty() ? fullHost : previewHost);

    if (host.contains(QStringLiteral("giphy")))
        return QStringLiteral("giphy");
    if (host.contains(QStringLiteral("tenor")))
        return QStringLiteral("tenor");
    if (host.contains(QStringLiteral("klipy")))
        return QStringLiteral("klipy");
    return QStringLiteral("gif");
}

} // namespace

ChatTextEdit::ChatTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setObjectName("MessageInput");
    document()->setDocumentMargin(0);
    setAcceptRichText(false);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // Set up font with Noto Color Emoji / Twemoji COLR fallback for
    // unicode emoji rendering on systems that lack a native color emoji font.
    QFont baseFont = font();
    baseFont.setPointSize(qMax(1, baseFont.pointSize() + 1));
    QStringList families = baseFont.families();
    families << QStringLiteral("Twemoji COLR")
             << QStringLiteral("Noto Color Emoji")
             << QStringLiteral("Segoe UI Emoji");
    baseFont.setFamilies(families);
    setFont(baseFont);

    setPlaceholderText("Message...");
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void ChatTextEdit::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        if (!(e->modifiers() & Qt::ShiftModifier)) {
            emit returnPressed();
            return;
        }
    }
    if (e->key() == Qt::Key_Escape) {
        emit escapePressed();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

static bool mimeHasLocalFiles(const QMimeData *source)
{
    if (!source->hasUrls())
        return false;
    const auto urls = source->urls();
    return std::any_of(urls.begin(), urls.end(),
                       [](const QUrl &url) { return url.isLocalFile(); });
}

bool ChatTextEdit::canInsertFromMimeData(const QMimeData *source) const
{
    return mimeHasLocalFiles(source) || source->hasImage() || QTextEdit::canInsertFromMimeData(source);
}

void ChatTextEdit::insertFromMimeData(const QMimeData *source)
{
    if (mimeHasLocalFiles(source)) {
        emit filesPasted(source->urls());
        return;
    }
    if (source->hasImage()) {
        QImage image = qvariant_cast<QImage>(source->imageData());
        if (!image.isNull()) {
            emit imagePasted(image);
        }
        // When the clipboard holds both image and text (e.g. browser screenshot
        // with selected text), handle both instead of dropping the text portion.
        // Create a mime data with only the text part to avoid double-inserting
        // the image via the parent class.
        if (source->hasText()) {
            QMimeData textData;
            textData.setText(source->text());
            if (source->hasHtml())
                textData.setHtml(source->html());
            QTextEdit::insertFromMimeData(&textData);
            return;
        }
        return;
    }
    QTextEdit::insertFromMimeData(source);
}

MessageInput::MessageInput(QWidget *parent) : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    // Flush, edge-to-edge bottom block: the input bar owns the full bottom
    // width and the internal #MessageInput box carries the visual padding.
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // Reply bar
    replyBar = new QWidget(this);
    replyBar->setVisible(false);
    auto *replyLayout = new QHBoxLayout(replyBar);
    replyLayout->setContentsMargins(8, 4, 4, 2);
    replyLayout->setSpacing(4);

    replyLabel = new QLabel(replyBar);
    replyLabel->setStyleSheet("color: #b5bac1; font-size: 12px;");
    replyLayout->addWidget(replyLabel, 1);

    replyCancelButton = new QToolButton(replyBar);
    replyCancelButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::X, Core::Theme::Token::PlaceholderText));
    replyCancelButton->setIconSize(QSize(14, 14));
    replyCancelButton->setFixedSize(16, 16);
    replyCancelButton->setAutoRaise(true);
    replyCancelButton->setCursor(Qt::PointingHandCursor);
    replyCancelButton->setStyleSheet("QToolButton { border: none; }");
    replyLayout->addWidget(replyCancelButton);

    connect(replyCancelButton, &QToolButton::clicked, this, &MessageInput::clearReplyTarget);

    replyBarOpacity = new QGraphicsOpacityEffect(replyBar);
    replyBarOpacity->setOpacity(1.0);
    replyBar->setGraphicsEffect(replyBarOpacity);

    outerLayout->addWidget(replyBar);

    attachmentPanel = new AttachmentPreviewPanel(this);
    outerLayout->addWidget(attachmentPanel);

    // Sticker preview chip (LOW #20)
    stickerPreviewLabel = new QLabel(this);
    stickerPreviewLabel->setVisible(false);
    stickerPreviewLabel->setStyleSheet(
            "QLabel { background: palette(highlight); color: palette(highlightedtext); "
            "border-radius: 4px; padding: 4px 8px; font-size: 12px; }");
    stickerPreviewLabel->setTextFormat(Qt::PlainText);
    stickerPreviewLabel->setCursor(Qt::PointingHandCursor);
    stickerPreviewLabel->installEventFilter(this);
    outerLayout->addWidget(stickerPreviewLabel);

    previewSplitter = new QSplitter(Qt::Vertical, this);
    previewSplitter->setChildrenCollapsible(false);
    previewSplitter->setHandleWidth(4);

    markdownPreview = new QTextBrowser(previewSplitter);
    markdownPreview->setObjectName("MarkdownInputPreview");
    markdownPreview->setVisible(false);
    markdownPreview->setOpenExternalLinks(false);
    markdownPreview->setFrameShape(QFrame::NoFrame);
    markdownPreview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    markdownPreview->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    markdownPreview->setStyleSheet(
            QString("#MarkdownInputPreview { background: #2b2d31; border: 1px solid #3f4147; "
                    "border-radius: %1px; padding: 6px; }")
                    .arg(Core::Theme::Manager::instance().roundness()));

    markdownPreviewDebounceTimer = new QTimer(this);
    markdownPreviewDebounceTimer->setSingleShot(true);
    markdownPreviewDebounceTimer->setInterval(150);
    connect(markdownPreviewDebounceTimer, &QTimer::timeout,
            this, &MessageInput::renderMarkdownPreview);

    // Text edit
    auto *inputContainer = new QWidget(this);
    auto *inputLayout = new QHBoxLayout(inputContainer);
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(0);

    auto *composeEmojiButton = new QToolButton(inputContainer);
    composeEmojiButton->setText(QStringLiteral("😀"));
    composeEmojiButton->setToolTip(tr("Insert emoji"));
    composeEmojiButton->setFixedWidth(28);
    composeEmojiButton->setStyleSheet(
            "QToolButton { border: none; color: #b5bac1; font-size: 16px; }"
            "QToolButton:hover { color: #ffffff; }");
    inputLayout->addWidget(composeEmojiButton);

    textEdit = new ChatTextEdit(inputContainer);
    setFocusProxy(textEdit);
    textEdit->installEventFilter(this);

    emojiNam = new QNetworkAccessManager(this);
    emojiPopup = new EmojiAutocompletePopup(this);
    emojiPopup->setNetworkAccessManager(emojiNam);
    emojiPopup->hide();

    slashPopup = new SlashCommandPopup(this);
    slashPopup->hide();

    mentionPopup = new MentionAutocompletePopup(this);
    mentionPopup->hide();

    connect(textEdit, &ChatTextEdit::returnPressed, [this]() {
        if (sendBlocked)
            return;
        QString txt = extractMessageText();

        // Include pending sticker if present (LOW #20)
        bool hasPendingSticker = pendingSticker.has_value();

        if (txt.trimmed().isEmpty() && !attachmentPanel->hasAttachments() && !hasPendingSticker)
            return;

        // Send sticker first if pending (separate API call from text)
        if (hasPendingSticker) {
            Core::Snowflake sid = pendingSticker->stickerId;
            emit stickerPicked(sid);
        }

        // Only emit sendMessage if there's actual text or attachments
        if (!txt.trimmed().isEmpty() || attachmentPanel->hasAttachments()) {
            Discord::ApplicationCommand command;
            QList<Discord::InteractionOptionValue> options;
            // A slash command can't carry attachments; if files are queued, send
            // as a normal message instead of silently dropping them. A bare "/"
            // (no command name) is also a literal message.
            const QString trimmed = txt.trimmed();
            const bool slashLike = !attachmentPanel->hasAttachments()
                                   && trimmed.startsWith(QLatin1Char('/'))
                                   && trimmed.mid(1).length() > 0;
            if (slashLike && tryParseSlashCommand(txt, &command, &options)) {
                if (!hasAllRequiredOptions(command, options)) {
                    emit slashCommandIncomplete(tr("Missing required option(s)."));
                    return;
                }
                emit slashCommandSend(command, options);
            } else if (slashLike) {
                // Leading '/' that isn't a known command: don't leak it as a
                // literal message (matches Discord's behavior).
                emit slashCommandIncomplete(tr("Unknown command. Type / to see available commands."));
            } else {
                emit sendMessage(txt, attachmentPanel->attachments());
            }
        }
    });

    connect(textEdit, &ChatTextEdit::escapePressed, this, [this]() {
        clearReplyTarget();
        attachmentPanel->clearAttachments();
    });

    slashQueryDebounce = new QTimer(this);
    slashQueryDebounce->setSingleShot(true);
    slashQueryDebounce->setInterval(250);
    connect(slashQueryDebounce, &QTimer::timeout, this, [this]() {
        // Emit even when empty so the full (unfiltered) command list is restored.
        emit slashQueryChanged(m_pendingSlashQuery);
        m_pendingSlashQuery.clear();
    });

    connect(textEdit->document(), &QTextDocument::contentsChanged, this, [this]() {
        // One text snapshot per keystroke shared by all popup updates — the
        // prefix helpers would otherwise each re-copy the whole document.
        const QString text = textEdit->toPlainText();
        updateEmojiPopup(text);
        updateSlashPopup(text);
        updateMentionPopup(text);
        updateMarkdownPreview();
        adjustHeight();
    });

    connect(textEdit, &ChatTextEdit::filesPasted, this, &MessageInput::queueAttachments);
    connect(textEdit, &ChatTextEdit::imagePasted, attachmentPanel, &AttachmentPreviewPanel::addImage);
    connect(attachmentPanel, &AttachmentPreviewPanel::attachmentsChanged, this, &MessageInput::adjustHeight);
    // Fade in attachment panel when attachments appear
    connect(attachmentPanel, &AttachmentPreviewPanel::attachmentsChanged, this, [this]() {
        if (attachmentPanel->isVisible() && attachmentPanel->hasAttachments())
            Core::AnimationUtils::fadeIn(attachmentPanel, 180);
    });
    connect(emojiPopup, &EmojiAutocompletePopup::emojiSelected, this, [this](const Core::EmojiCatalogItem &item) {
        insertEmojiCompletion(item);
    });
    // Debounced emoji query applied: reflect the outcome in show/hide state
    // (this replaces the old per-keystroke hasResults() show/hide).
    connect(emojiPopup, &EmojiAutocompletePopup::queryApplied, this, [this]() {
        if (emojiPopup->hasResults())
            showEmojiPopup();
        else
            hideEmojiPopup();
    });
    connect(slashPopup, &SlashCommandPopup::commandSelected, this, [this](const Discord::ApplicationCommand &command) {
        insertSlashCompletion(command);
    });
    connect(slashPopup, &SlashCommandPopup::suggestionSelected, this, [this](const QString &insertText) {
        insertSlashArgument(insertText);
    });
    connect(mentionPopup, &MentionAutocompletePopup::mentionSelected, this, [this](const MentionItem &item) {
        insertMentionCompletion(item);
    });
    connect(composeEmojiButton, &QToolButton::clicked, this, [this]() {
        const QString emoji = pickEmoji(this, tr("Emoji Picker"), tr("Search emoji"),
                                        guildOrder, currentGuildId);
        if (emoji.isEmpty())
            return;
        insertText(emoji);
    });

    stickerPickerButton = new QToolButton(inputContainer);
    stickerPickerButton->setVisible(false);
    stickerPickerButton->setText(QStringLiteral("🎨"));
    stickerPickerButton->setToolTip(tr("Stickers"));
    stickerPickerButton->setFixedWidth(28);
    stickerPickerButton->setStyleSheet(
            "QToolButton { border: none; color: #b5bac1; font-size: 16px; }"
            "QToolButton:hover { color: #ffffff; }");
    inputLayout->addWidget(stickerPickerButton);
    stickerNam = new QNetworkAccessManager(this);
    gifNam = new QNetworkAccessManager(this);

    // GIF picker button
    gifPickerButton = new QToolButton(inputContainer);
    gifPickerButton->setText(QStringLiteral("GIF"));
    gifPickerButton->setToolTip(tr("Search and send GIFs"));
    gifPickerButton->setFixedWidth(36);
    gifPickerButton->setStyleSheet(
            "QToolButton { border: none; color: #b5bac1; font-size: 11px; font-weight: bold; }"
            "QToolButton:hover { color: #ffffff; }");
    inputLayout->addWidget(gifPickerButton);
    connect(gifPickerButton, &QToolButton::clicked, this, &MessageInput::pickGif);

    // GIF download progress label (MEDIUM #11)
    gifProgressLabel = new QLabel(inputContainer);
    gifProgressLabel->setVisible(false);
    gifProgressLabel->setStyleSheet(
            "QLabel { color: #b5bac1; font-size: 11px; padding: 0 4px; }");
    inputLayout->addWidget(gifProgressLabel);

    connect(stickerPickerButton, &QToolButton::clicked, this, [this]() {
        if (availableStickers.isEmpty())
            return;

        StickerPickerDialog dialog(this);
        dialog.setWindowTitle(tr("Sticker Picker"));

        QList<StickerPackGroup> packs;
        for (auto it = availableStickers.constBegin(); it != availableStickers.constEnd(); ++it) {
            StickerPackGroup pack;
            pack.guildId = it.key();
            if (it.key() == stickerGuildId) {
                pack.guildName = stickerGuildName;
                pack.guildIconHash = stickerGuildIconHash;
            } else {
                pack.guildName = QStringLiteral("Guild %1").arg(QString::number(static_cast<quint64>(it.key())));
            }
            pack.stickers = it.value();
            packs.append(pack);
        }

        dialog.setStickerPacks(packs);
        if (stickerGuildIconProvider)
            dialog.setGuildIconProvider(stickerGuildIconProvider);

        connect(&dialog, &StickerPickerDialog::stickerSelected, this,
                [this](Core::Snowflake sid) {
                    // Show sticker preview before sending (LOW #20)
                    // Find the sticker and download its preview
                    for (auto it = availableStickers.constBegin(); it != availableStickers.constEnd(); ++it) {
                        for (const auto &sticker : it.value()) {
                            if (sticker.id == sid) {
                                pendingSticker = PendingSticker{sid, QImage(), sticker.name.get()};
                                // Show preview in the label
                                stickerPreviewLabel->setText(
                                    tr("Sticker: %1  [loading preview…]  [x]").arg(sticker.name.get()));
                                stickerPreviewLabel->setVisible(true);
                                // Download preview image
                                QUrl previewUrl = Discord::Cdn::stickerImage(
                                    sid, sticker.formatType.get(), 64);
                                QNetworkReply *previewReply = stickerNam->get(QNetworkRequest(previewUrl));
                                // Guard the sticker preview label so the lambda can
                                // detect if the MessageInput was destroyed before the
                                // network reply arrives (defense-in-depth).
                                QPointer<QLabel> previewGuard = stickerPreviewLabel;
                                connect(previewReply, &QNetworkReply::finished, this,
                                        [this, previewGuard, previewReply, sid]() {
                                            previewReply->deleteLater();
                                            if (!previewGuard)
                                                return;
                                            if (previewReply->error() != QNetworkReply::NoError) {
                                                if (pendingSticker && pendingSticker->stickerId == sid) {
                                                    stickerPreviewLabel->setText(tr("Sticker: %1  [preview unavailable]  [x]")
                                                                                 .arg(pendingSticker->description));
                                                }
                                                return;
                                            }
                                            QImage img;
                                            if (!img.loadFromData(previewReply->readAll())) {
                                                if (pendingSticker && pendingSticker->stickerId == sid) {
                                                    stickerPreviewLabel->setText(tr("Sticker: %1  [preview unavailable]  [x]")
                                                                                 .arg(pendingSticker->description));
                                                }
                                                return;
                                            }
                                            if (pendingSticker && pendingSticker->stickerId == sid) {
                                                pendingSticker->previewImage = img;
                                                stickerPreviewLabel->setText(
                                                    tr("Sticker: %1  [preview ready]  [x]").arg(pendingSticker->description));
                                            }
                                        });
                                adjustHeight();
                                return;
                            }
                        }
                    }
                });

        dialog.exec();
    });
    connect(previewSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (!markdownPreviewVisible)
            return;
        const auto sizes = previewSplitter->sizes();
        if (!sizes.isEmpty())
            markdownPreviewHeight = std::max(56, sizes.first());
        adjustHeight();
    });

    inputLayout->addWidget(textEdit);
    previewSplitter->addWidget(markdownPreview);
    previewSplitter->addWidget(inputContainer);
    outerLayout->addWidget(previewSplitter);

    auto *previewToggleRow = new QWidget(this);
    auto *previewToggleLayout = new QHBoxLayout(previewToggleRow);
    previewToggleLayout->setContentsMargins(0, 4, 0, 0);
    previewToggleLayout->setSpacing(0);
    previewToggleLayout->addStretch();

    markdownPreviewToggle = new QToolButton(previewToggleRow);
    markdownPreviewToggle->setCheckable(true);
    markdownPreviewToggle->setText(tr("Preview"));
    markdownPreviewToggle->setToolTip(tr("Toggle markdown preview"));
    markdownPreviewToggle->setStyleSheet(
            "QToolButton { border: none; color: #b5bac1; font-size: 12px; padding: 2px 6px; }"
            "QToolButton:hover, QToolButton:checked { color: #ffffff; }");
    previewToggleLayout->addWidget(markdownPreviewToggle);
    outerLayout->addWidget(previewToggleRow);

    connect(markdownPreviewToggle, &QToolButton::toggled, this,
            &MessageInput::setMarkdownPreviewVisible);

    setAcceptDrops(true);

    adjustHeight();
}

void MessageInput::setPlaceholder(const QString &name)
{
    textEdit->setPlaceholderText(name);
}

void MessageInput::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

bool MessageInput::eventFilter(QObject *watched, QEvent *event)
{
    // Sticker preview click to dismiss (LOW #20)
    if (watched == stickerPreviewLabel && event->type() == QEvent::MouseButtonPress) {
        clearPendingSticker();
        return true;
    }

    if (watched == textEdit) {
        if (event->type() == QEvent::FocusIn) {
            // Only animate on first focus, not repeated focus-in events
            if (!textEdit->property("hadFocus").toBool()) {
                textEdit->setProperty("hadFocus", true);
                Core::AnimationUtils::popIn(textEdit, 120);
            }
        }
        if (event->type() == QEvent::KeyPress && emojiPopup &&
            emojiPopup->isVisible()) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            switch (keyEvent->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            emojiPopup->acceptCurrent();
            hideEmojiPopup();
            return true;
        case Qt::Key_Escape:
            hideEmojiPopup();
            return true;
        case Qt::Key_Up:
            emojiPopup->moveSelection(-1);
            return true;
        case Qt::Key_Down:
            emojiPopup->moveSelection(1);
            return true;
        default:
            break;
        }
    }

    if (event->type() == QEvent::KeyPress && slashPopup &&
        slashPopup->isVisible()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            slashPopup->acceptCurrent();
            hideSlashPopup();
            return true;
        case Qt::Key_Escape:
            hideSlashPopup();
            return true;
        case Qt::Key_Up:
            slashPopup->moveSelection(-1);
            return true;
        case Qt::Key_Down:
            slashPopup->moveSelection(1);
            return true;
        default:
            break;
        }
    }

    if (event->type() == QEvent::KeyPress && mentionPopup &&
        mentionPopup->isVisible()) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        switch (keyEvent->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Tab:
            mentionPopup->acceptCurrent();
            hideMentionPopup();
            return true;
        case Qt::Key_Escape:
            hideMentionPopup();
            return true;
        case Qt::Key_Up:
            mentionPopup->moveSelection(-1);
            return true;
        case Qt::Key_Down:
            mentionPopup->moveSelection(1);
            return true;
        default:
            break;
        }
    }

    }
    return QWidget::eventFilter(watched, event);
}

void MessageInput::clear()
{
    textEdit->clear();
    clearReplyTarget();
    clearPendingSticker();
    attachmentPanel->clearAttachments();
    hideEmojiPopup();
    if (slashQueryDebounce)
        slashQueryDebounce->stop();
    m_pendingSlashQuery.clear();
    // Properly delete movies held via QPointer (MEDIUM #17)
    for (auto it = activeEmojiMovies.begin(); it != activeEmojiMovies.end(); ++it) {
        if (QMovie *m = it.value())
            m->deleteLater();
    }
    activeEmojiMovies.clear();
    emojiGifCache.clear();
    emojiMarkerSequence = 0; // reset to avoid overflow (LOW #22)
    adjustHeight();
}

void MessageInput::clearPendingSticker()
{
    pendingSticker.reset();
    if (stickerPreviewLabel)
        stickerPreviewLabel->setVisible(false);
    adjustHeight();
}

void MessageInput::queueAttachments(const QList<QUrl> &urls)
{
    attachmentPanel->addFiles(urls);
    textEdit->setFocus();
}

void MessageInput::setMaxUploadSize(qint64 bytes)
{
    attachmentPanel->setMaxFileSize(bytes);
}

void MessageInput::dragEnterEvent(QDragEnterEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (event->mimeData()->hasUrls() &&
        std::any_of(urls.begin(), urls.end(),
                    [](const QUrl &url) { return url.isLocalFile(); }))
        event->acceptProposedAction();
}

void MessageInput::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    event->acceptProposedAction();
    queueAttachments(event->mimeData()->urls());
}

void MessageInput::setReplyTarget(Core::Snowflake messageId, const QString &authorName,
                                  const QString &contentSnippet)
{
    replyMessageId = messageId;
    QString snippet = contentSnippet;
    snippet.replace('\n', ' ');
    if (snippet.length() > 100)
        snippet = snippet.left(100) + "...";

    const QString escapedAuthor = authorName.toHtmlEscaped();
    const QString escapedSnippet = snippet.toHtmlEscaped();
    replyLabel->setText(tr("Replying to <b>%1</b>%2")
                                .arg(escapedAuthor,
                                     escapedSnippet.isEmpty()
                                             ? QString()
                                             : QStringLiteral(" %1").arg(escapedSnippet)));
    showReplyBar();
    adjustHeight();
    textEdit->setFocus();
}

void MessageInput::clearReplyTarget()
{
    if (!replyMessageId.isValid())
        return;

    replyMessageId = Core::Snowflake::Invalid;
    replyLabel->clear();
    hideReplyBar();
}

void MessageInput::setSendBlocked(bool blocked)
{
    sendBlocked = blocked;
}

void MessageInput::insertText(const QString &text)
{
    // Try to find a matching emoji item from the catalog
    for (const auto &item : Core::EmojiCatalog::items()) {
        if (item.selectionValue() == text) {
            insertEmojiInline(item);
            return;
        }
    }
    textEdit->insertPlainText(text);
    textEdit->setFocus();
}

QString MessageInput::currentEmojiPrefix(int *startPosition) const
{
    return currentEmojiPrefix(textEdit->toPlainText(), startPosition);
}

QString MessageInput::currentEmojiPrefix(const QString &text, int *startPosition) const
{
    QTextCursor cursor = textEdit->textCursor();
    const int position = cursor.position();
    const int colonPosition = text.lastIndexOf(':', position - 1);
    if (colonPosition < 0)
        return {};

    const QString prefix = text.mid(colonPosition + 1, position - colonPosition - 1);
    static const QRegularExpression validPrefix(QStringLiteral("^[A-Za-z0-9_+-]+$"));
    if (prefix.length() < 2 || !validPrefix.match(prefix).hasMatch())
        return {};

    if (colonPosition > 0) {
        const QChar beforeColon = text.at(colonPosition - 1);
        if (beforeColon.isLetterOrNumber() || beforeColon == QLatin1Char('_'))
            return {};
    }

    if (startPosition)
        *startPosition = colonPosition;
    return prefix;
}

void MessageInput::updateEmojiCompleter()
{
    updateEmojiPopup(textEdit->toPlainText());
}

void MessageInput::refreshEmojiPopup()
{
    // The popup queries EmojiCatalog dynamically, so no pre-population needed.
}

void MessageInput::showEmojiPopup()
{
    if (!emojiPopup)
        return;
    // Mutually exclusive with the other autocomplete popups.
    hideSlashPopup();
    hideMentionPopup();
    QTextCursor cursor = textEdit->textCursor();
    QRect cursorRect = textEdit->cursorRect(cursor);
    QPoint bottomLeft = textEdit->mapToGlobal(QPoint(cursorRect.left(), cursorRect.bottom() + 2));
    emojiPopup->move(bottomLeft);
    emojiPopup->show();

    // Clamp popup to screen boundaries (MEDIUM #18). Prefer the screen the
    // popup landed on, falling back to the window's screen for multi-monitor
    // setups where the position may be between screens.
    QScreen *screen = QGuiApplication::screenAt(bottomLeft);
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        return;

    const QRect screenGeo = screen->availableGeometry();
    const QRect popupGeo = emojiPopup->geometry();
    if (!screenGeo.contains(popupGeo)) {
        QPoint clamped = popupGeo.topLeft();
        clamped.setX(qMax(screenGeo.left(), qMin(clamped.x(),
                      screenGeo.right() - popupGeo.width())));
        clamped.setY(qMax(screenGeo.top(), qMin(clamped.y(),
                      screenGeo.bottom() - popupGeo.height())));
        emojiPopup->move(clamped);
    }
}

void MessageInput::hideEmojiPopup()
{
    if (emojiPopup) {
        // Drop any debounced-but-not-yet-applied query so a stale timer can't
        // show the popup again after the emoji context was removed.
        emojiPopup->cancelPendingQuery();
        emojiPopup->hide();
    }
}

void MessageInput::updateEmojiPopup(const QString &text)
{
    int startPosition = -1;
    const QString prefix = currentEmojiPrefix(text, &startPosition);
    if (prefix.isEmpty()) {
        hideEmojiPopup();
        return;
    }

    if (!emojiPopup)
        return;

    // The popup debounces burst typing internally; show/hide is driven by its
    // queryApplied() signal instead of reacting to every keystroke.
    emojiPopup->setQuery(prefix);
}

void MessageInput::insertEmojiCompletion(const Core::EmojiCatalogItem &item)
{
    int startPosition = -1;
    if (currentEmojiPrefix(&startPosition).isEmpty())
        return;

    QTextCursor cursor = textEdit->textCursor();
    cursor.setPosition(startPosition, QTextCursor::MoveAnchor);
    cursor.setPosition(textEdit->textCursor().position(), QTextCursor::KeepAnchor);
    textEdit->setTextCursor(cursor);

    insertEmojiInline(item);
    hideEmojiPopup();
}

void MessageInput::showSlashPopup()
{
    if (!slashPopup)
        return;
    // Mutually exclusive with the mention/emoji popups so their key handling
    // can't fight over Up/Down/Enter.
    hideMentionPopup();
    hideEmojiPopup();
    QTextCursor cursor = textEdit->textCursor();
    QRect cursorRect = textEdit->cursorRect(cursor);
    QPoint bottomLeft = textEdit->mapToGlobal(QPoint(cursorRect.left(), cursorRect.bottom() + 2));
    slashPopup->move(bottomLeft);
    slashPopup->show();

    QScreen *screen = QGuiApplication::screenAt(bottomLeft);
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        return;

    const QRect screenGeo = screen->availableGeometry();
    const QRect popupGeo = slashPopup->geometry();
    if (!screenGeo.contains(popupGeo)) {
        QPoint clamped = popupGeo.topLeft();
        clamped.setX(qMax(screenGeo.left(), qMin(clamped.x(),
                      screenGeo.right() - popupGeo.width())));
        clamped.setY(qMax(screenGeo.top(), qMin(clamped.y(),
                      screenGeo.bottom() - popupGeo.height())));
        slashPopup->move(clamped);
    }
}

void MessageInput::hideSlashPopup()
{
    if (slashPopup)
        slashPopup->hide();
}

namespace {

void collectSubCommandSuggestions(const QList<Discord::ApplicationCommandOption> &options,
                                  QStringList *names, QStringList *descriptions,
                                  QStringList *insertTexts)
{
    for (const auto &opt : options) {
        const auto type = opt.type.get();
        if (type != Discord::ApplicationCommandOptionType::SUB_COMMAND
            && type != Discord::ApplicationCommandOptionType::SUB_COMMAND_GROUP)
            continue;
        names->append(opt.name.get());
        descriptions->append(opt.description.hasValue() ? opt.description.get() : QString());
        insertTexts->append(opt.name.get());
    }
}

void suggestChoicesForOption(const QList<Discord::ApplicationCommandOption> &options,
                             const QStringList &valueTokens, QStringList *names,
                             QStringList *descriptions, QStringList *insertTexts)
{
    const int filled = valueTokens.size();
    if (filled < 0 || filled >= options.size())
        return;
    const auto &opt = options[filled];
    if (opt.type.get() != Discord::ApplicationCommandOptionType::STRING)
        return;
    if (!opt.choices.hasValue() || opt.choices->isEmpty())
        return;
    for (const auto &choice : *opt.choices) {
        names->append(choice.name.get());
        descriptions->append(QString());
        insertTexts->append(choice.value.get());
    }
}

// Determines argument suggestions for a command given the fully-typed tokens
// after the command name (the partial token, if any, is excluded by the caller).
void computeSlashSuggestions(const Discord::ApplicationCommand &cmd, const QStringList &tokens,
                             QStringList *names, QStringList *descriptions,
                             QStringList *insertTexts)
{
    using namespace Discord;
    QList<ApplicationCommandOption> options;
    if (cmd.options.hasValue())
        options = *cmd.options;
    if (options.isEmpty())
        return;

    // Sub-command groups: [group] [sub] ...
    if (options.first().type.get() == ApplicationCommandOptionType::SUB_COMMAND_GROUP) {
        if (tokens.isEmpty()) {
            collectSubCommandSuggestions(options, names, descriptions, insertTexts);
            return;
        }
        const ApplicationCommandOption *group = nullptr;
        for (const auto &opt : options) {
            if (opt.name.get() == tokens.first()) {
                group = &opt;
                break;
            }
        }
        if (!group)
            return;
        if (tokens.size() == 1) {
            if (group->options.hasValue())
                collectSubCommandSuggestions(*group->options, names, descriptions, insertTexts);
            return;
        }
        const ApplicationCommandOption *sub = nullptr;
        if (group->options.hasValue()) {
            for (const auto &opt : *group->options) {
                if (opt.name.get() == tokens.at(1)) {
                    sub = &opt;
                    break;
                }
            }
        }
        if (!sub)
            return;
        options.clear();
        if (sub->options.hasValue())
            options = *sub->options;
        suggestChoicesForOption(options, tokens.mid(2), names, descriptions, insertTexts);
        return;
    }

    // Top-level sub-commands: [sub] ...
    if (options.first().type.get() == ApplicationCommandOptionType::SUB_COMMAND) {
        if (tokens.isEmpty()) {
            collectSubCommandSuggestions(options, names, descriptions, insertTexts);
            return;
        }
        const ApplicationCommandOption *sub = nullptr;
        for (const auto &opt : options) {
            if (opt.name.get() == tokens.first()) {
                sub = &opt;
                break;
            }
        }
        if (!sub)
            return;
        options.clear();
        if (sub->options.hasValue())
            options = *sub->options;
        suggestChoicesForOption(options, tokens.mid(1), names, descriptions, insertTexts);
        return;
    }

    // No sub-commands: options are value-level.
    suggestChoicesForOption(options, tokens, names, descriptions, insertTexts);
}

} // namespace

void MessageInput::updateSlashPopup(const QString &text)
{
    if (!slashPopup)
        return;

    // Emoji autocomplete runs first in the update chain and has precedence: a
    // `:name` token (e.g. inside slash args) should win over slash suggestions.
    if (emojiPopup && emojiPopup->isVisible()) {
        hideSlashPopup();
        if (slashQueryDebounce)
            slashQueryDebounce->stop();
        m_pendingSlashQuery.clear();
        return;
    }

    if (!text.startsWith(QLatin1Char('/'))) {
        hideSlashPopup();
        if (slashQueryDebounce)
            slashQueryDebounce->stop();
        m_pendingSlashQuery.clear();
        return;
    }

    const QTextCursor cursor = textEdit->textCursor();
    const int position = cursor.position();
    const QString afterSlash = text.mid(1, qMax(0, position - 1));

    // Command-name phase: no space yet -> suggest matching commands.
    if (!afterSlash.contains(QLatin1Char(' '))) {
        static const QRegularExpression validPrefix(QStringLiteral("^[A-Za-z0-9_-]*$"));
        if (!validPrefix.match(afterSlash).hasMatch()) {
            hideSlashPopup();
            if (slashQueryDebounce)
                slashQueryDebounce->stop();
            m_pendingSlashQuery.clear();
            return;
        }
        slashPopup->setQuery(afterSlash);
        if (slashPopup->hasResults())
            showSlashPopup();
        else
            hideSlashPopup();

        // Debounced server-side command search for better discoverability.
        if (slashQueryDebounce) {
            m_pendingSlashQuery = afterSlash;
            slashQueryDebounce->start();
        }
        return;
    }

    // Argument phase: resolve the command and suggest sub-commands/choices.
    const int spaceIdx = afterSlash.indexOf(QLatin1Char(' '));
    const QString commandName = afterSlash.left(spaceIdx);
    const QString argumentText = afterSlash.mid(spaceIdx + 1);

    // The command name is settled; no more server-side command search.
    if (slashQueryDebounce)
        slashQueryDebounce->stop();
    m_pendingSlashQuery.clear();

    const Discord::ApplicationCommand *cmd = nullptr;
    for (const auto &c : m_availableCommands) {
        if (c.name.get().toCaseFolded() == commandName.toCaseFolded()) {
            cmd = &c;
            break;
        }
    }
    if (!cmd) {
        hideSlashPopup();
        return;
    }

    QStringList rawTokens = argumentText.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QString partialWord;
    QStringList completeTokens = rawTokens;
    if (!argumentText.isEmpty() && !argumentText.endsWith(QLatin1Char(' '))) {
        partialWord = rawTokens.takeLast();
        completeTokens = rawTokens;
    }

    QStringList names, descriptions, insertTexts;
    computeSlashSuggestions(*cmd, completeTokens, &names, &descriptions, &insertTexts);
    if (names.isEmpty()) {
        hideSlashPopup();
        return;
    }

    slashPopup->setSuggestions(names, descriptions, insertTexts, partialWord);
    if (slashPopup->hasResults())
        showSlashPopup();
    else
        hideSlashPopup();
}

void MessageInput::insertSlashCompletion(const Discord::ApplicationCommand &command)
{
    QTextCursor cursor = textEdit->textCursor();
    const QString docText = textEdit->toPlainText();
    const int docLen = docText.length();

    // Replace the whole command-name token (which starts at index 1) regardless
    // of where the cursor is, so clicking mid-name can't mangle it.
    int tokenEnd = 1;
    while (tokenEnd < docLen && !docText.at(tokenEnd).isSpace())
        ++tokenEnd;

    cursor.setPosition(0, QTextCursor::MoveAnchor);
    cursor.setPosition(tokenEnd, QTextCursor::KeepAnchor);
    cursor.insertText(QStringLiteral("/%1").arg(command.name.get()));
    if (tokenEnd >= docLen)
        cursor.insertText(QStringLiteral(" "));
    hideSlashPopup();
    textEdit->setFocus();
}

void MessageInput::insertSlashArgument(const QString &text)
{
    QTextCursor cursor = textEdit->textCursor();
    const int position = cursor.position();
    const QString docText = textEdit->toPlainText();

    // Replace the partial argument token the user was typing (from after the
    // last space up to the cursor), otherwise `/cmd su<Tab>` becomes `/cmd susub1 `.
    int start = position;
    while (start > 0 && !docText.at(start - 1).isSpace())
        --start;
    cursor.setPosition(start, QTextCursor::MoveAnchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    cursor.insertText(text + QLatin1Char(' '));

    hideSlashPopup();
    textEdit->setFocus();
}

void MessageInput::setAvailableCommands(const QList<Discord::ApplicationCommand> &commands)
{
    m_availableCommands = commands;
    if (slashPopup)
        slashPopup->setCommands(commands);
    // Refresh the popup immediately so fetched results aren't one keystroke late.
    updateSlashPopup(textEdit->toPlainText());
}

namespace {

QStringList tokenizeArguments(const QString &text)
{
    QStringList tokens;
    QString current;
    bool inQuotes = false;
    QChar quoteChar;
    for (const QChar c : text) {
        if (inQuotes) {
            if (c == quoteChar) {
                inQuotes = false;
            } else {
                current.append(c);
            }
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            inQuotes = true;
            quoteChar = c;
        } else if (c.isSpace()) {
            if (!current.isEmpty()) {
                tokens.append(current);
                current.clear();
            }
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty())
        tokens.append(current);
    return tokens;
}

Core::Snowflake extractMentionSnowflake(const QString &token, bool *ok)
{
    *ok = false;
    QString inner = token;
    if (token.startsWith(QLatin1Char('<')) && token.endsWith(QLatin1Char('>'))) {
        inner = token.mid(1, token.length() - 2);
        if (inner.startsWith(QLatin1Char('@')) && inner.length() > 1 && inner.at(1) == QLatin1Char('&'))
            inner = inner.mid(2);
        else if (inner.startsWith(QLatin1Char('@')) || inner.startsWith(QLatin1Char('#')))
            inner = inner.mid(1);
    }
    bool valid = false;
    const quint64 id = inner.toULongLong(&valid);
    if (valid && id != 0) {
        *ok = true;
        return Core::Snowflake(id);
    }
    return {};
}

QList<Discord::InteractionOptionValue> parseCommandOptions(
        const QList<Discord::ApplicationCommandOption> &options, QStringList &tokens)
{
    using namespace Discord;
    QList<InteractionOptionValue> result;
    if (options.isEmpty())
        return result;

    const auto firstType = options.first().type.get();

    // Sub-command group: match group name then sub-command name.
    if (firstType == ApplicationCommandOptionType::SUB_COMMAND_GROUP) {
        if (tokens.isEmpty())
            return result;
        const QString groupName = tokens.takeFirst();
        const ApplicationCommandOption *group = nullptr;
        for (const auto &opt : options) {
            if (opt.name.get() == groupName) {
                group = &opt;
                break;
            }
        }
        if (!group)
            return result;

        InteractionOptionValue gv;
        gv.type = static_cast<int>(ApplicationCommandOptionType::SUB_COMMAND_GROUP);
        gv.name = group->name.get();

        if (!tokens.isEmpty() && group->options.hasValue()) {
            const QString subName = tokens.takeFirst();
            for (const auto &sub : *group->options) {
                if (sub.name.get() == subName) {
                    InteractionOptionValue sv;
                    sv.type = static_cast<int>(ApplicationCommandOptionType::SUB_COMMAND);
                    sv.name = sub.name.get();
                    if (sub.options.hasValue())
                        sv.options = parseCommandOptions(*sub.options, tokens);
                    gv.options.append(sv);
                    break;
                }
            }
        }
        result.append(gv);
        return result;
    }

    // Top-level sub-commands.
    if (firstType == ApplicationCommandOptionType::SUB_COMMAND) {
        if (tokens.isEmpty())
            return result;
        const QString subName = tokens.takeFirst();
        for (const auto &opt : options) {
            if (opt.name.get() == subName) {
                InteractionOptionValue sv;
                sv.type = static_cast<int>(ApplicationCommandOptionType::SUB_COMMAND);
                sv.name = opt.name.get();
                if (opt.options.hasValue())
                    sv.options = parseCommandOptions(*opt.options, tokens);
                result.append(sv);
                break;
            }
        }
        return result;
    }

    // Regular options, consumed in declaration order.
    const int count = options.size();
    for (int i = 0; i < count; ++i) {
        const auto &opt = options[i];
        const auto type = opt.type.get();
        if (type == ApplicationCommandOptionType::SUB_COMMAND
            || type == ApplicationCommandOptionType::SUB_COMMAND_GROUP)
            continue;
        if (tokens.isEmpty())
            break;

        InteractionOptionValue v;
        v.type = static_cast<int>(type);
        v.name = opt.name.get();

        switch (type) {
        case ApplicationCommandOptionType::STRING: {
            const bool isLast = (i == count - 1);
            if (isLast && tokens.size() > 1) {
                v.value = tokens.join(QLatin1Char(' '));
                tokens.clear();
            } else {
                v.value = tokens.takeFirst();
            }
            break;
        }
        case ApplicationCommandOptionType::INTEGER: {
            static const QRegularExpression intRe(QStringLiteral("^-?\\d+$"));
            const QString tok = tokens.first();
            if (!intRe.match(tok).hasMatch()) {
                tokens.removeFirst();
                continue;
            }
            tokens.removeFirst();
            v.value = static_cast<double>(tok.toLongLong());
            break;
        }
        case ApplicationCommandOptionType::NUMBER: {
            static const QRegularExpression numRe(QStringLiteral("^-?\\d+(\\.\\d+)?$"));
            const QString tok = tokens.first();
            if (!numRe.match(tok).hasMatch()) {
                tokens.removeFirst();
                continue;
            }
            tokens.removeFirst();
            v.value = tok.toDouble();
            break;
        }
        case ApplicationCommandOptionType::BOOLEAN: {
            const QString t = tokens.first().toCaseFolded();
            if (t == QLatin1String("true") || t == QLatin1String("yes") || t == QLatin1String("1")) {
                tokens.removeFirst();
                v.value = true;
            } else if (t == QLatin1String("false") || t == QLatin1String("no") || t == QLatin1String("0")) {
                tokens.removeFirst();
                v.value = false;
            } else {
                // Invalid token: consume it and skip this option, matching the
                // INTEGER/NUMBER behavior, so it can't bleed into the next option.
                tokens.removeFirst();
                continue;
            }
            break;
        }
        case ApplicationCommandOptionType::USER:
        case ApplicationCommandOptionType::ROLE:
        case ApplicationCommandOptionType::MENTIONABLE:
        case ApplicationCommandOptionType::CHANNEL: {
            const QString tok = tokens.takeFirst();
            bool ok = false;
            const Core::Snowflake id = extractMentionSnowflake(tok, &ok);
            v.value = ok ? id.toString() : tok;
            break;
        }
        default:
            v.value = tokens.takeFirst();
            break;
        }

        result.append(v);
    }

    return result;
}

bool optionValueFilled(const Discord::InteractionOptionValue &v)
{
    // A matched sub-command/group is "filled" by being selected; required nested
    // values are checked by hasAllRequiredOptionsImpl on opt.options.
    if (v.type == static_cast<int>(Discord::ApplicationCommandOptionType::SUB_COMMAND)
        || v.type == static_cast<int>(Discord::ApplicationCommandOptionType::SUB_COMMAND_GROUP))
        return true;
    return v.isScalar();
}

bool hasAllRequiredOptionsImpl(const QList<Discord::ApplicationCommandOption> &options,
                               const QList<Discord::InteractionOptionValue> &parsed)
{
    for (const auto &opt : options) {
        const auto type = opt.type.get();
        const bool required = (type == Discord::ApplicationCommandOptionType::SUB_COMMAND
                               || type == Discord::ApplicationCommandOptionType::SUB_COMMAND_GROUP)
                                      ? true
                                      : opt.required.getOr(false);
        if (!required)
            continue;

        bool filled = false;
        for (const auto &v : parsed) {
            if (v.name != opt.name.get())
                continue;
            if (!optionValueFilled(v)) {
                filled = false;
                break;
            }
            if (v.type == static_cast<int>(Discord::ApplicationCommandOptionType::SUB_COMMAND)
                || v.type == static_cast<int>(Discord::ApplicationCommandOptionType::SUB_COMMAND_GROUP)) {
                const QList<Discord::ApplicationCommandOption> subOptions = opt.options.getOr({});
                if (!hasAllRequiredOptionsImpl(subOptions, v.options)) {
                    filled = false;
                    break;
                }
            }
            filled = true;
            break;
        }
        if (!filled)
            return false;
    }
    return true;
}

bool hasAllRequiredOptions(const Discord::ApplicationCommand &command,
                           const QList<Discord::InteractionOptionValue> &parsed)
{
    const QList<Discord::ApplicationCommandOption> options = command.options.getOr({});
    return hasAllRequiredOptionsImpl(options, parsed);
}

} // namespace

bool MessageInput::tryParseSlashCommand(const QString &text, Discord::ApplicationCommand *command,
                                        QList<Discord::InteractionOptionValue> *options) const
{
    // A slash command must start at column 0 (matching the popup's trigger
    // condition); leading whitespace means a normal message.
    if (!text.startsWith(QLatin1Char('/')))
        return false;
    const QString trimmed = text;

    int space = -1;
    for (int i = 1; i < trimmed.length(); ++i) {
        if (trimmed.at(i).isSpace()) {
            space = i;
            break;
        }
    }
    const QString name = (space < 0) ? trimmed.mid(1) : trimmed.mid(1, space - 1);
    const QString rest = (space < 0) ? QString() : trimmed.mid(space + 1);

    const Discord::ApplicationCommand *match = nullptr;
    for (const auto &cmd : m_availableCommands) {
        if (cmd.name.get().toCaseFolded() == name.toCaseFolded()) {
            match = &cmd;
            break;
        }
    }
    if (!match)
        return false;

    *command = *match;

    QStringList tokens = tokenizeArguments(rest);
    QList<Discord::ApplicationCommandOption> cmdOptions;
    if (match->options.hasValue())
        cmdOptions = *match->options;
    *options = parseCommandOptions(cmdOptions, tokens);
    return true;
}

QString MessageInput::currentMentionPrefix(int *startPosition, QChar *trigger) const
{
    return currentMentionPrefix(textEdit->toPlainText(), startPosition, trigger);
}

QString MessageInput::currentMentionPrefix(const QString &text, int *startPosition, QChar *trigger) const
{
    QTextCursor cursor = textEdit->textCursor();
    const int position = cursor.position();

    int triggerPos = -1;
    QChar trig;
    for (int i = position - 1; i >= 0; --i) {
        const QChar c = text.at(i);
        if (c == '@' || c == '#') {
            if (i == 0 || text.at(i - 1).isSpace()) {
                triggerPos = i;
                trig = c;
                break;
            }
        }
    }
    if (triggerPos < 0)
        return {};

    const QString prefix = text.mid(triggerPos + 1, position - triggerPos - 1);
    static const QRegularExpression validPrefix(QStringLiteral("^[A-Za-z0-9_-]*$"));
    if (!validPrefix.match(prefix).hasMatch())
        return {};

    if (startPosition)
        *startPosition = triggerPos;
    if (trigger)
        *trigger = trig;
    return prefix;
}

void MessageInput::showMentionPopup()
{
    if (!mentionPopup)
        return;
    // Mutually exclusive with the slash/emoji popups so their key handling
    // can't fight.
    hideSlashPopup();
    hideEmojiPopup();
    QTextCursor cursor = textEdit->textCursor();
    QRect cursorRect = textEdit->cursorRect(cursor);
    QPoint bottomLeft = textEdit->mapToGlobal(QPoint(cursorRect.left(), cursorRect.bottom() + 2));
    mentionPopup->move(bottomLeft);
    mentionPopup->show();

    QScreen *screen = QGuiApplication::screenAt(bottomLeft);
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        return;

    const QRect screenGeo = screen->availableGeometry();
    const QRect popupGeo = mentionPopup->geometry();
    if (!screenGeo.contains(popupGeo)) {
        QPoint clamped = popupGeo.topLeft();
        clamped.setX(qMax(screenGeo.left(), qMin(clamped.x(),
                      screenGeo.right() - popupGeo.width())));
        clamped.setY(qMax(screenGeo.top(), qMin(clamped.y(),
                      screenGeo.bottom() - popupGeo.height())));
        mentionPopup->move(clamped);
    }
}

void MessageInput::hideMentionPopup()
{
    if (mentionPopup)
        mentionPopup->hide();
}

void MessageInput::updateMentionPopup(const QString &text)
{
    int startPosition = -1;
    QChar trigger;
    const QString prefix = currentMentionPrefix(text, &startPosition, &trigger);
    if (trigger.isNull()) {
        hideMentionPopup();
        return;
    }
    if (!mentionPopup)
        return;

    // Emoji has precedence when both triggers could apply (e.g. `:sm` inside a
    // message that also parses as a slash command).
    if (emojiPopup && emojiPopup->isVisible()) {
        hideMentionPopup();
        return;
    }

    // The popup pre-splits its items by kind in setItems() (called from
    // setAvailableMentions), so per-keystroke we only re-run the match on the
    // relevant subset instead of re-filtering + re-copying the whole list.
    const MentionItem::Kind kind = trigger == QLatin1Char('#')
                                           ? MentionItem::Kind::Channel
                                           : MentionItem::Kind::User;
    mentionPopup->setQuery(prefix, kind);

    if (mentionPopup->hasResults()) {
        showMentionPopup();
    } else {
        hideMentionPopup();
    }
}

void MessageInput::insertMentionCompletion(const MentionItem &item)
{
    int startPosition = -1;
    QChar trigger;
    [[maybe_unused]] const QString unusedPrefix = currentMentionPrefix(&startPosition, &trigger);
    if (trigger.isNull()) {
        // The mention context disappeared (e.g. the trigger was deleted); hide
        // the popup so it doesn't linger with stale items.
        hideMentionPopup();
        return;
    }

    QString text;
    switch (item.kind) {
    case MentionItem::Kind::Role:
        text = QStringLiteral("<@&%1>").arg(item.id.toString());
        break;
    case MentionItem::Kind::Channel:
        text = QStringLiteral("<#%1>").arg(item.id.toString());
        break;
    case MentionItem::Kind::User:
    default:
        text = QStringLiteral("<@%1>").arg(item.id.toString());
        break;
    }

    QTextCursor cursor = textEdit->textCursor();
    cursor.setPosition(startPosition, QTextCursor::MoveAnchor);
    cursor.setPosition(textEdit->textCursor().position(), QTextCursor::KeepAnchor);
    textEdit->setTextCursor(cursor);
    cursor.insertText(text + QStringLiteral(" "));
    hideMentionPopup();
}

void MessageInput::setAvailableMentions(const QList<MentionItem> &items)
{
    m_availableMentions = items;
    if (mentionPopup)
        mentionPopup->setItems(items);
}

void MessageInput::refreshEmojiCompleter()
{
    // The EmojiAutocompletePopup queries EmojiCatalog dynamically on each
    // setQuery() call, so there is no model to refresh here.
    // This method is kept for API compatibility with callers in MainWindow.
}

void MessageInput::setAvailableStickers(QHash<Core::Snowflake, QList<Discord::Sticker>> stickers)
{
    availableStickers = std::move(stickers);
    stickerPickerButton->setVisible(!availableStickers.isEmpty());
}

void MessageInput::setGuildOrder(const QStringList &orderedGuildIds)
{
    guildOrder = orderedGuildIds;
}

void MessageInput::setCurrentGuildId(const Core::Snowflake &guildId)
{
    currentGuildId = guildId;
}

void MessageInput::setCompact(bool compact)
{
    if (compactMode == compact)
        return;
    compactMode = compact;
    adjustHeight();
}

void MessageInput::setStickerGuildInfo(Core::Snowflake guildId, const QString &guildName,
                                       const QString &guildIconHash,
                                       std::function<QUrl(Core::Snowflake, const QString &)> iconProvider)
{
    stickerGuildId = guildId;
    stickerGuildName = guildName;
    stickerGuildIconHash = guildIconHash;
    stickerGuildIconProvider = std::move(iconProvider);
}

void MessageInput::setMarkdownPreviewVisible(bool visible)
{
    markdownPreviewVisible = visible;
    markdownPreview->setVisible(visible);
    markdownPreviewDebounceTimer->stop();
    lastMarkdownPreviewText.reset();
    renderMarkdownPreview();
    adjustHeight();
    textEdit->setFocus();
}

void MessageInput::updateMarkdownPreview()
{
    if (!markdownPreviewVisible)
        return;

    // Debounce: re-rendering parses the whole message, so don't do it on
    // every keystroke while the user is still typing.
    markdownPreviewDebounceTimer->start();
}

void MessageInput::renderMarkdownPreview()
{
    if (!markdownPreviewVisible)
        return;

    const QString text = textEdit->toPlainText();
    if (lastMarkdownPreviewText && text == *lastMarkdownPreviewText)
        return;
    lastMarkdownPreviewText = text;

    if (text.trimmed().isEmpty()) {
        markdownPreview->setHtml(markdownPreviewHtml(QStringLiteral(
                "<span style=\"color: #949ba4;\">Markdown preview</span>")));
        return;
    }

    const auto nodes = markdownParser.parse(text);
    const QString rawHtml = markdownParser.toHtml(nodes);
    markdownPreview->setHtml(markdownPreviewHtml(sanitizeHtml(rawHtml)));
}

int MessageInput::collapsedContentHeight() const
{
    const int splitterHeight = previewSplitter->height();
    int h = splitterHeight + markdownPreviewToggle->parentWidget()->sizeHint().height() + 8;
    if (replyBar->isVisible())
        h += replyBar->sizeHint().height();
    if (attachmentPanel->isVisible())
        h += attachmentPanel->sizeHint().height();
    return h;
}

void MessageInput::adjustHeight()
{
    int contentHeight = textEdit->document()->size().height();

    // Compact keeps its slim profile; the default bar is taller and uses
    // tighter padding so the field grows while the total block stays compact.
    const int vPadding = compactMode ? 10 : 14;
    const int minHeight = compactMode ? 30 : 52;
    int newHeight = contentHeight + vPadding;

    if (newHeight < minHeight)
        newHeight = minHeight;
    if (newHeight > 200)
        newHeight = 200;

    if (newHeight >= 200)
        textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    else
        textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    textEdit->setFixedHeight(newHeight);
    const int splitterHeight = newHeight + (markdownPreviewVisible ? markdownPreviewHeight : 0);
    previewSplitter->setFixedHeight(splitterHeight);
    if (markdownPreviewVisible)
        previewSplitter->setSizes({markdownPreviewHeight, newHeight});

    // The block's height is a minimum (collapsed) plus a maximum that grows
    // with the status strip, so the strip can slide open without snapping
    // the rest of the input around.
    const int collapsed = collapsedContentHeight();
    setMinimumHeight(collapsed);
    if (!stripAnimGroup_)
        setMaximumHeight(collapsed + (statusStrip_ ? statusStrip_->maximumHeight() : 0));
}

void MessageInput::setStatusStrip(QWidget *strip)
{
    if (!strip || strip == statusStrip_)
        return;
    statusStrip_ = strip;
    strip->setParent(this);
    // Start collapsed: the strip slides out only while typing/slowmode is
    // active, so an idle input bar consumes no extra height.
    strip->setMaximumHeight(0);
    static_cast<QVBoxLayout *>(layout())->insertWidget(0, strip);
    adjustHeight();
}

void MessageInput::setStatusStripActive(bool active)
{
    if (!statusStrip_)
        return;
    const int natural = statusStrip_->sizeHint().height();
    const int stripTarget = active ? natural : 0;
    const int blockTarget = collapsedContentHeight() + stripTarget;

    if (stripAnimGroup_) {
        stripAnimGroup_->stop();
        stripAnimGroup_->deleteLater();
        stripAnimGroup_ = nullptr;
    }

    auto *group = new QParallelAnimationGroup(this);
    auto *stripAnim = new QPropertyAnimation(statusStrip_, "maximumHeight", group);
    stripAnim->setDuration(Core::AnimationUtils::duration(160));
    stripAnim->setStartValue(statusStrip_->maximumHeight());
    stripAnim->setEndValue(stripTarget);
    stripAnim->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(stripAnim);

    auto *blockAnim = new QPropertyAnimation(this, "maximumHeight", group);
    blockAnim->setDuration(Core::AnimationUtils::duration(160));
    blockAnim->setStartValue(maximumHeight());
    blockAnim->setEndValue(blockTarget);
    blockAnim->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(blockAnim);

    stripAnimGroup_ = group;
    connect(group, &QParallelAnimationGroup::finished, this, [this, group]() {
        if (stripAnimGroup_ == group)
            stripAnimGroup_ = nullptr;
        group->deleteLater();
    });
    group->start();
}

void MessageInput::insertEmojiInline(const Core::EmojiCatalogItem &item)
{
    QTextCursor cursor = textEdit->textCursor();

    if (!item.isCustom()) {
        cursor.insertText(item.unicodeEmoji);
        textEdit->setTextCursor(cursor);
        return;
    }

    const QString cacheKey = QStringLiteral("custom_emoji_%1").arg(item.customId);
    const QString emojiValue = item.selectionValue();

    if (item.animated) {
        QMovie *movie = activeEmojiMovies.value(emojiValue);
        if (movie) {
            QImage frame = movie->currentImage();
            if (!frame.isNull()) {
                cursor.insertImage(frame, emojiValue);
                textEdit->setTextCursor(cursor);
                return;
            }
        }

        QByteArray cachedGif = emojiGifCache.value(emojiValue);
        if (!cachedGif.isEmpty()) {
            startAnimatedEmoji(emojiValue, cachedGif);
            movie = activeEmojiMovies.value(emojiValue);
            if (movie) {
                QImage frame = movie->currentImage();
                if (!frame.isNull()) {
                    cursor.insertImage(frame, emojiValue);
                    textEdit->setTextCursor(cursor);
                    return;
                }
                cleanEmojiAnimation(emojiValue);
            } else {
                emojiGifCache.remove(emojiValue);
            }
        }

        const QString marker = makeEmojiMarker(emojiValue);
        cursor.insertText(marker);
        textEdit->setTextCursor(cursor);

        QNetworkReply *reply = emojiNam->get(QNetworkRequest(QUrl(item.cdnUrl(64))));
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, marker, emojiValue]() {
                    reply->deleteLater();
                    if (reply->error() != QNetworkReply::NoError) {
                        removeMarker(marker);
                        return;
                    }

                    const QByteArray gifData = reply->readAll();
                    if (gifData.isEmpty()) {
                        removeMarker(marker);
                        return;
                    }

                    emojiGifCache[emojiValue] = gifData;
                    startAnimatedEmoji(emojiValue, gifData);
                    QMovie *m = activeEmojiMovies.value(emojiValue);
                    if (!m) {
                        emojiGifCache.remove(emojiValue);
                        removeMarker(marker);
                        return;
                    }

                    QImage first = m->currentImage();
                    if (first.isNull()) {
                        cleanEmojiAnimation(emojiValue);
                        removeMarker(marker);
                        return;
                    }

                    if (!replaceMarker(marker, first, emojiValue))
                        cleanEmojiAnimation(emojiValue);
                });
        return;
    }

    QPixmap cached;
    if (QPixmapCache::find(cacheKey, &cached)) {
        cursor.insertImage(cached.toImage(), emojiValue);
        textEdit->setTextCursor(cursor);
        return;
    }

    const QString marker = makeEmojiMarker(emojiValue);
    cursor.insertText(marker);
    textEdit->setTextCursor(cursor);

    QNetworkReply *reply = emojiNam->get(QNetworkRequest(QUrl(item.cdnUrl(32))));
    connect(reply, &QNetworkReply::finished, this, [this, reply, marker, cacheKey, emojiValue]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            removeMarker(marker);
            return;
        }

        QPixmap pix;
        if (!pix.loadFromData(reply->readAll())) {
            removeMarker(marker);
            return;
        }

        const int inlineSize = emojiInlineSize();
        pix = pix.scaled(inlineSize, inlineSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QPixmapCache::insert(cacheKey, pix);
        replaceMarker(marker, pix.toImage(), emojiValue);
    });
}

bool MessageInput::replaceMarker(const QString &marker, const QImage &frame,
                                  const QString &emojiValue)
{
    QTextDocument *doc = textEdit->document();
    QTextCursor findCursor(doc);
    findCursor.movePosition(QTextCursor::End);
    findCursor = doc->find(marker, findCursor, QTextDocument::FindBackward);
    if (findCursor.isNull())
        return false;
    findCursor.insertImage(frame, emojiValue);
    return true;
}

void MessageInput::removeMarker(const QString &marker)
{
    QTextDocument *doc = textEdit->document();
    QTextCursor findCursor(doc);
    findCursor.movePosition(QTextCursor::Start);
    findCursor = doc->find(marker, findCursor);
    if (!findCursor.isNull())
        findCursor.removeSelectedText();
}

void MessageInput::startAnimatedEmoji(const QString &emojiValue,
                                       const QByteArray &gifData)
{
    if (activeEmojiMovies.contains(emojiValue))
        return;

    const int inlineSize = emojiInlineSize();

    auto *movie = new QMovie(this);
    auto *buffer = new QBuffer(movie);
    buffer->setData(gifData);
    buffer->open(QIODevice::ReadOnly);
    movie->setDevice(buffer);
    movie->setFormat(QByteArrayLiteral("gif"));
    movie->setScaledSize(QSize(inlineSize, inlineSize));

    if (!movie->isValid()) {
        delete movie;
        return;
    }

    activeEmojiMovies.insert(emojiValue, QPointer<QMovie>(movie));
    movie->start();

    // Set the initial frame as the document image resource so it appears
    // immediately before the first frameChanged signal fires.
    QTextDocument *doc = textEdit->document();
    doc->addResource(QTextDocument::ImageResource, QUrl(emojiValue),
                     QVariant(movie->currentImage()));

    connect(movie, &QMovie::frameChanged, this, [this, emojiValue, inlineSize](int) {
        QMovie *m = activeEmojiMovies.value(emojiValue);
        if (!m)
            return;

        QImage frame = m->currentImage();
        if (frame.isNull())
            return;

        // Update the document image resource so the QTextCursor/QTextDocument
        // picks up the new frame and repaints the viewport.
        QTextDocument *doc = textEdit->document();
        doc->addResource(QTextDocument::ImageResource, QUrl(emojiValue),
                         QVariant(frame));

        // Force the text edit's viewport to repaint the area containing
        // this image resource.
        textEdit->viewport()->update();
    });

    // Auto-cleanup when the movie finishes (MEDIUM #17)
    connect(movie, &QMovie::finished, this, [this, emojiValue]() {
        if (activeEmojiMovies.contains(emojiValue))
            cleanEmojiAnimation(emojiValue);
    });
}

void MessageInput::cleanEmojiAnimation(const QString &emojiValue)
{
    auto it = activeEmojiMovies.find(emojiValue);
    if (it != activeEmojiMovies.end()) {
        if (QMovie *movie = it.value()) {
            movie->stop();
            movie->deleteLater();
        }
        activeEmojiMovies.erase(it);
    }
    emojiGifCache.remove(emojiValue);
}

int MessageInput::emojiInlineSize() const
{
    // Match the font's x-height rather than full line height for a more
    // natural visual baseline with surrounding text.
    const QFontMetrics fm(textEdit->font());
    const int xHeight = fm.xHeight();
    // Clamp to reasonable bounds: at least 16px, at most 28px.
    return std::clamp(xHeight * 2, 16, 28);
}

QString MessageInput::extractMessageText() const
{
    QString result;
    QTextBlock block = textEdit->document()->begin();
    bool first = true;
    while (block.isValid()) {
        if (!first)
            result += '\n';
        first = false;
        for (auto it = block.begin(); it != block.end(); ++it) {
            QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            if (fragment.charFormat().isImageFormat()) {
                QTextImageFormat fmt = fragment.charFormat().toImageFormat();
                result += fmt.name();
            } else {
                QString text = fragment.text();
                static const QRegularExpression markerRe(
                        QStringLiteral("\uE000\\d+\u2063([^\uE000]+)\uE000"));
                text.replace(markerRe, QStringLiteral("\\1"));
                result += text;
            }
        }
        block = block.next();
    }
    return result;
}

void MessageInput::pickGif()
{
    GifPickerDialog dialog(this);
    connect(&dialog, &GifPickerDialog::gifSelected, this,
            [this](const Discord::GifItem &gif) {
                // Choose the best URL for the full-size GIF
                QUrl url = gif.full.url;
                if (!url.isValid())
                    url = gif.preview.url;
                if (!url.isValid())
                    return;

                const QString prefix = gifFilenamePrefix(gif);
                const QString baseName = gif.title.isEmpty() ? gif.id : gif.title;
                QString filename = baseName.isEmpty()
                        ? QStringLiteral("%1.gif").arg(prefix)
                        : QStringLiteral("%1_%2.gif").arg(prefix, baseName);
                // Sanitize filename
                filename.remove(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_.-]")));
                if (filename.isEmpty())
                    filename = QStringLiteral("gif.gif");

                downloadAndAttachGif(url, filename);
            });
    dialog.exec();
}

void MessageInput::downloadAndAttachGif(const QUrl &url, const QString &filename)
{
    if (!gifNam)
        gifNam = new QNetworkAccessManager(this);

    // Show progress indicator (MEDIUM #11)
    gifProgressLabel->setText(tr("Downloading GIF\u2026"));
    gifProgressLabel->setVisible(true);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    QNetworkReply *reply = gifNam->get(request);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) {
                    gifProgressLabel->setText(
                        tr("Downloading GIF\u2026 %1%")
                            .arg(static_cast<int>(received * 100 / total)));
                } else {
                    gifProgressLabel->setText(
                        tr("Downloading GIF\u2026 %1 KB")
                            .arg(received / 1024));
                }
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply, filename]() {
        reply->deleteLater();
        gifProgressLabel->setVisible(false);

        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "Failed to download GIF:" << reply->errorString();
            attachmentPanel->showTransientError(tr("Couldn't download GIF"));
            return;
        }

        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            attachmentPanel->showTransientError(tr("Downloaded GIF was empty"));
            return;
        }

        // Auto-detect the container instead of forcing "gif": klipy media is
        // frequently served as webp (see ImageManager), and a forced-format
        // decode of that data fails — the GIF would never attach. Sniffing
        // also lets us label the attachment with the real extension/mime so
        // Discord and the chat renderer treat it correctly on the other side.
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        QByteArray detectedFormat = QImageReader::imageFormat(&buf);
        if (detectedFormat.isEmpty())
            detectedFormat = QByteArrayLiteral("gif");
        buf.seek(0);

        QImageReader reader(&buf, detectedFormat);
        QImage preview = reader.read();
        if (preview.isNull()) {
            attachmentPanel->showTransientError(tr("Couldn't decode GIF preview"));
            return;
        }

        // Re-label the attachment with the real extension + mime so a webp
        // payload isn't sent as "image/gif" (which renders broken in chat).
        const QString fmtName = QString::fromLatin1(detectedFormat);
        const QString ext = QLatin1Char('.') + fmtName;
        QString realFilename = filename;
        if (!realFilename.endsWith(ext, Qt::CaseInsensitive)) {
            const int dot = realFilename.lastIndexOf(QLatin1Char('.'));
            realFilename = (dot > 0 ? realFilename.left(dot) : realFilename) + ext;
        }
        const QString mime = QStringLiteral("image/") + fmtName;

        // Queue via the attachment panel with full GIF data preserved
        attachmentPanel->addGifData(data, preview, realFilename, mime);
    });
}

void MessageInput::showReplyBar()
{
    ++replyBarAnimationGeneration;

    if (replyBarFadeAnimation) {
        replyBarFadeAnimation->stop();
        replyBarFadeAnimation->deleteLater();
        replyBarFadeAnimation = nullptr;
    }

    qreal startOpacity = 0.01;
    if (replyBarOpacity) {
        startOpacity = std::max<qreal>(0.01, replyBarOpacity->opacity());
        if (replyBar->isVisible() && startOpacity >= 0.99) {
            replyBarOpacity->setOpacity(1.0);
            return;
        }
    }

    replyBar->show();
    if (replyBarOpacity)
        replyBarOpacity->setOpacity(startOpacity);

    replyBarFadeAnimation = new QPropertyAnimation(replyBarOpacity, "opacity", replyBar);
    replyBarFadeAnimation->setDuration(Core::AnimationConfig::instance().scaled(180));
    replyBarFadeAnimation->setStartValue(startOpacity);
    replyBarFadeAnimation->setEndValue(1.0);
    replyBarFadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(replyBarFadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        replyBarFadeAnimation = nullptr;
        if (replyBarOpacity)
            replyBarOpacity->setOpacity(1.0);
    });
    replyBarFadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void MessageInput::hideReplyBar()
{
    ++replyBarAnimationGeneration;
    const quint64 generation = replyBarAnimationGeneration;

    if (replyBarFadeAnimation) {
        replyBarFadeAnimation->stop();
        replyBarFadeAnimation->deleteLater();
        replyBarFadeAnimation = nullptr;
    }

    replyBarFadeAnimation = new QPropertyAnimation(replyBarOpacity, "opacity", replyBar);
    replyBarFadeAnimation->setDuration(Core::AnimationConfig::instance().scaled(120));
    replyBarFadeAnimation->setStartValue(replyBarOpacity ? replyBarOpacity->opacity() : 1.0);
    replyBarFadeAnimation->setEndValue(0.0);
    replyBarFadeAnimation->setEasingCurve(QEasingCurve::InCubic);
    connect(replyBarFadeAnimation, &QPropertyAnimation::finished, this, [this, generation]() {
        replyBarFadeAnimation = nullptr;
        if (generation != replyBarAnimationGeneration || replyMessageId.isValid())
            return;
        replyBar->hide();
        if (replyBarOpacity)
            replyBarOpacity->setOpacity(1.0);
        adjustHeight();
    });
    replyBarFadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

QString MessageInput::makeEmojiMarker(const QString &emojiValue)
{
    ++emojiMarkerSequence;
    // Use a robust delimiter sequence. The emojiValue may contain pipe '|' or
    // other characters, so we bracket with Private Use Area chars that cannot
    // appear in user-typed text or in emojiValue. Use a zero-width joiner
    // sequence as inner separator to eliminate delimiter collision.
    return QStringLiteral("\uE000%1\u2063%2\uE000").arg(emojiMarkerSequence).arg(emojiValue);
}

} // namespace UI
} // namespace Acheron




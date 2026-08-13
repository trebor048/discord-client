#include "MessageInput.hpp"
#include "AttachmentPreviewPanel.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/EmojiCatalog.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Entities.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "UI/Dialogs/GifPickerDialog.hpp"
#include "UI/Dialogs/StickerPickerDialog.hpp"
#include "UI/Widgets/Chat/EmojiAutocompletePopup.hpp"
#include "Core/Markdown/Parser.hpp"
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
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextImageFormat>
#include <QToolButton>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
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
    outerLayout->setContentsMargins(4, 0, 4, 0);
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

    emojiPickerButton = new QToolButton(replyBar);
    emojiPickerButton->setText(QStringLiteral("☺"));
    emojiPickerButton->setToolTip(tr("Open emoji picker"));
    emojiPickerButton->setFixedSize(20, 20);
    emojiPickerButton->setStyleSheet(
            "QToolButton { border: none; color: #b5bac1; font-size: 14px; }"
            "QToolButton:hover { color: #ffffff; }");
    replyLayout->addWidget(emojiPickerButton);
    connect(emojiPickerButton, &QToolButton::clicked, this, [this]() {
        const QString emoji = pickEmoji(this, tr("Emoji Picker"), tr("Search emoji"),
                                        guildOrder, currentGuildId);
        if (emoji.isEmpty())
            return;
        insertText(emoji);
    });

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
            "#MarkdownInputPreview { background: #2b2d31; border: 1px solid #3f4147; "
            "border-radius: 6px; padding: 6px; }");

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
        if (!txt.trimmed().isEmpty() || attachmentPanel->hasAttachments())
            emit sendMessage(txt, attachmentPanel->attachments());
    });

    connect(textEdit, &ChatTextEdit::escapePressed, this, [this]() {
        clearReplyTarget();
        attachmentPanel->clearAttachments();
    });

    connect(textEdit->document(), &QTextDocument::contentsChanged, this, [this]() {
        updateEmojiPopup();
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
    QTextCursor cursor = textEdit->textCursor();
    const int position = cursor.position();
    const QString text = textEdit->toPlainText();
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
    updateEmojiPopup();
}

void MessageInput::refreshEmojiPopup()
{
    // The popup queries EmojiCatalog dynamically, so no pre-population needed.
}

void MessageInput::showEmojiPopup()
{
    if (!emojiPopup)
        return;
    QTextCursor cursor = textEdit->textCursor();
    QRect cursorRect = textEdit->cursorRect(cursor);
    QPoint bottomLeft = textEdit->mapToGlobal(QPoint(cursorRect.left(), cursorRect.bottom() + 2));
    emojiPopup->move(bottomLeft);
    emojiPopup->show();

    // Clamp popup to screen boundaries (MEDIUM #18)
    QRect screenGeo = QGuiApplication::primaryScreen()->availableGeometry();
    QRect popupGeo = emojiPopup->geometry();
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
    if (emojiPopup)
        emojiPopup->hide();
}

void MessageInput::updateEmojiPopup()
{
    int startPosition = -1;
    const QString prefix = currentEmojiPrefix(&startPosition);
    if (prefix.isEmpty()) {
        hideEmojiPopup();
        return;
    }

    if (!emojiPopup)
        return;

    emojiPopup->setQuery(prefix);

    if (emojiPopup->hasResults()) {
        showEmojiPopup();
    } else {
        hideEmojiPopup();
    }
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
    updateMarkdownPreview();
    adjustHeight();
    textEdit->setFocus();
}

void MessageInput::updateMarkdownPreview()
{
    if (!markdownPreviewVisible)
        return;

    const QString text = textEdit->toPlainText();
    if (text.trimmed().isEmpty()) {
        markdownPreview->setHtml(markdownPreviewHtml(QStringLiteral(
                "<span style=\"color: #949ba4;\">Markdown preview</span>")));
        return;
    }

    Core::Markdown::Parser parser;
    const auto nodes = parser.parse(text);
    const QString rawHtml = parser.toHtml(nodes);
    markdownPreview->setHtml(markdownPreviewHtml(sanitizeHtml(rawHtml)));
}

void MessageInput::adjustHeight()
{
    int contentHeight = textEdit->document()->size().height();

    const int vPadding = compactMode ? 10 : 20;
    const int minHeight = compactMode ? 30 : 44;
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

    int totalHeight = splitterHeight + markdownPreviewToggle->parentWidget()->sizeHint().height() + 12;
    if (replyBar->isVisible())
        totalHeight += replyBar->sizeHint().height();
    if (attachmentPanel->isVisible())
        totalHeight += attachmentPanel->sizeHint().height();

    setFixedHeight(totalHeight);
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

        // Load the first frame as preview image
        QBuffer buf;
        buf.setData(data);
        buf.open(QIODevice::ReadOnly);
        QImageReader reader(&buf, QByteArrayLiteral("gif"));
        QImage preview = reader.read();
        if (preview.isNull()) {
            attachmentPanel->showTransientError(tr("Couldn't decode GIF preview"));
            return;
        }

        // Queue via the attachment panel with full GIF data preserved
        attachmentPanel->addGifData(data, preview, filename);
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
    replyBarFadeAnimation->setDuration(180);
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
    replyBarFadeAnimation->setDuration(120);
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




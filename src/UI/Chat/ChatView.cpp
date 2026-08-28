#include "ChatView.hpp"

#include "Core/MessageManager.hpp"
#include "Core/Settings.hpp"
#include "Core/EmojiCatalog.hpp"
#include "Core/Theme/Manager.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include <QMenu>
#include <QGraphicsOpacityEffect>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextCursor>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QPainter>
#include <QLineEdit>
#include <QLabel>
#include <QDesktopServices>
#include <QToolTip>
#include <QSettings>
#include <algorithm>

#include "Core/TimeUtils.hpp"
#include "UI/Dialogs/ConfirmPopup.hpp"
#include "UI/ImageViewer.hpp"
#include "UI/AttachmentGallery.hpp"
#include "UI/Dialogs/VideoPlayerDialog.hpp"

#include <QMediaPlayer>
#include <QAudioOutput>

namespace Acheron {
namespace UI {
namespace {

// Lazy-loaded voice message audio player — only created when user clicks a voice message.
// Kept file-static to avoid pulling Qt Multimedia headers into ChatView.hpp. Volume is
// shared with the video player dialog under the "media/volume" QSettings key.
static QAudioOutput *voiceAudioOutput()
{
    static QAudioOutput *audioOut = nullptr;
    if (!audioOut) {
        audioOut = new QAudioOutput;
        audioOut->setVolume(qreal(QSettings().value("media/volume", 100).toInt()) / 100.0);
    }
    return audioOut;
}

static QMediaPlayer *voicePlayer()
{
    static QMediaPlayer *player = nullptr;
    if (!player) {
        player = new QMediaPlayer;
        player->setAudioOutput(voiceAudioOutput());
    }
    return player;
}

QString normalizeReactionEmoji(QString emoji)
{
    emoji = emoji.trimmed();
    if (emoji.isEmpty())
        return {};

    static const QRegularExpression customEmojiRe(
            R"(^<a?:([A-Za-z0-9_]+):([0-9]+)>$)");
    const auto match = customEmojiRe.match(emoji);
    if (match.hasMatch())
        return match.captured(1) + ":" + match.captured(2);

    // Discord normalizes reaction names without the U+FE0F variation selector
    // (e.g. "❤" instead of "❤️"), so strip it (and any other trailing
    // variation selectors) so both sides compare equal.
    emoji.remove(QChar(0xFE0F));
    return emoji;
}

// Temporary overlay painted over the chat viewport during a channel switch.
// Draws a snapshot of the previous channel with a painter-level opacity so it
// never touches QGraphicsOpacityEffect — keeping it fully independent from the
// jump-to-bottom button's own opacity animation.
class ChannelFadeOverlay : public QWidget
{
public:
    ChannelFadeOverlay(const QPixmap &snapshot, QWidget *parent)
        : QWidget(parent), snapshot(snapshot)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

    void setOpacity(qreal opacity)
    {
        if (qFuzzyCompare(opacity, this->opacity))
            return;
        this->opacity = opacity;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (snapshot.isNull() || opacity <= 0.0)
            return;
        QPainter painter(this);
        painter.setOpacity(opacity);
        painter.drawPixmap(rect(), snapshot);
    }

private:
    QPixmap snapshot;
    qreal opacity = 1.0;
};

// Floating mini-player for voice messages. A frameless child of the ChatView so it
// stays put while the chat scrolls. Drives the shared, lazy voicePlayer() and shares
// its volume with the video player dialog under the "media/volume" QSettings key.
class VoicePlayerPanel : public QWidget
{
public:
    explicit VoicePlayerPanel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("voicePlayerPanel"));
        setStyleSheet(QStringLiteral(
            "#voicePlayerPanel {"
            "  background: palette(window);"
            "  border: 1px solid palette(mid);"
            "  border-radius: %1px;"
            "}"
            "QPushButton {"
            "  background: transparent;"
            "  color: palette(text);"
            "  border: none;"
            "  border-radius: 4px;"
            "  padding: 4px 8px;"
            "}"
            "QPushButton:hover { color: palette(highlight); }"
            "QLabel { color: palette(text); background: transparent; }"
            "QSlider::groove:horizontal { height: 4px; background: palette(mid); border-radius: 2px; }"
            "QSlider::sub-page:horizontal { background: palette(highlight); border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 10px; margin: -3px 0; border-radius: 5px; background: palette(text); }"
        ).arg(Core::Theme::Manager::instance().roundness()));

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 6, 8, 6);
        layout->setSpacing(8);

        playButton = new QPushButton(QStringLiteral("▶"), this);
        playButton->setToolTip(tr("Play / Pause"));
        layout->addWidget(playButton);

        positionSlider = new QSlider(Qt::Horizontal, this);
        positionSlider->setRange(0, 0);
        positionSlider->setMinimumWidth(140);
        positionSlider->setToolTip(tr("Seek"));
        layout->addWidget(positionSlider, 1);

        timeLabel = new QLabel(QStringLiteral("00:00 / 00:00"), this);
        timeLabel->setToolTip(tr("Current time / total time"));
        layout->addWidget(timeLabel);

        volumeSlider = new QSlider(Qt::Horizontal, this);
        volumeSlider->setRange(0, 100);
        volumeSlider->setValue(QSettings().value("media/volume", 100).toInt());
        volumeSlider->setFixedWidth(70);
        volumeSlider->setToolTip(tr("Volume"));
        layout->addWidget(volumeSlider);

        auto *closeButton = new QPushButton(QStringLiteral("✕"), this);
        closeButton->setToolTip(tr("Close"));
        layout->addWidget(closeButton);

        QMediaPlayer *vp = voicePlayer();
        connect(playButton, &QPushButton::clicked, this, [this]() {
            QMediaPlayer *p = voicePlayer();
            if (p->playbackState() == QMediaPlayer::PlayingState)
                p->pause();
            else
                p->play();
        });
        connect(positionSlider, &QSlider::sliderPressed, this, [this]() { sliderDragging = true; });
        connect(positionSlider, &QSlider::sliderMoved, this, [this](int pos) {
            timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") +
                               formatTime(voicePlayer()->duration()));
            voicePlayer()->setPosition(pos);
        });
        connect(positionSlider, &QSlider::sliderReleased, this, [this]() {
            sliderDragging = false;
            voicePlayer()->setPosition(positionSlider->value());
        });
        connect(volumeSlider, &QSlider::valueChanged, this, [this](int value) {
            voiceAudioOutput()->setVolume(qreal(value) / 100.0);
            QSettings().setValue("media/volume", value);
        });
        connect(closeButton, &QPushButton::clicked, this, &QWidget::hide);

        connect(vp, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
            if (sliderDragging)
                return;
            updateSlider(position);
        });
        connect(vp, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
            positionSlider->setRange(0, int(duration));
            updateSlider(voicePlayer()->position());
        });
        connect(vp, &QMediaPlayer::playbackStateChanged, this, [this]() {
            const bool playing = voicePlayer()->playbackState() == QMediaPlayer::PlayingState;
            playButton->setText(playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
        });
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            hide();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void updateSlider(qint64 position)
    {
        positionSlider->blockSignals(true);
        positionSlider->setValue(int(position));
        positionSlider->blockSignals(false);
        timeLabel->setText(formatTime(position) + QStringLiteral(" / ") +
                           formatTime(voicePlayer()->duration()));
    }

    static QString formatTime(qint64 ms)
    {
        const qint64 totalSecs = ms / 1000;
        const qint64 mins = totalSecs / 60;
        const qint64 secs = totalSecs % 60;
        return QString("%1:%2").arg(mins, 2, 10, QLatin1Char('0'))
                               .arg(secs, 2, 10, QLatin1Char('0'));
    }

    QPushButton *playButton = nullptr;
    QSlider *positionSlider = nullptr;
    QLabel *timeLabel = nullptr;
    QSlider *volumeSlider = nullptr;
    bool sliderDragging = false;
};
} // namespace

ChatView::ChatView(QWidget *parent) : QListView(parent), hoveredRow(-1), hoveredChar(-1)
{
    setMouseTracking(true);
    setSelectionMode(QAbstractItemView::NoSelection);
    setUniformItemSizes(false);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    verticalScrollBar()->setSingleStep(40);
    setAutoScroll(false);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    inlineEditWidget = new QTextEdit(viewport());
    inlineEditWidget->setVisible(false);
    inlineEditWidget->setFrameStyle(QFrame::Box);
    inlineEditWidget->setLineWidth(2);
    inlineEditWidget->installEventFilter(this);

    searchPanel = new QWidget(viewport());
    searchPanel->setObjectName(QStringLiteral("chatSearchPanel"));
    searchPanel->setVisible(false);
    searchPanel->setStyleSheet(
            QStringLiteral("#chatSearchPanel { background: palette(window); border: 1px solid palette(mid); border-radius: %1px; }")
                    .arg(Core::Theme::Manager::instance().roundness()));
    auto *searchLayout = new QHBoxLayout(searchPanel);
    searchLayout->setContentsMargins(8, 4, 8, 4);
    searchLayout->setSpacing(6);
    searchEdit = new QLineEdit(searchPanel);
    searchEdit->setPlaceholderText(tr("Search messages"));
    searchEdit->installEventFilter(this);
    searchCountLabel = new QLabel(searchPanel);
    searchCountLabel->setMinimumWidth(48);
    searchCountLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    searchLayout->addWidget(searchEdit, 1);
    searchLayout->addWidget(searchCountLabel);
    // Debounce search input: rescanning the loaded history per keystroke is
    // too expensive, so wait for a short pause before matching.
    searchDebounceTimer = new QTimer(this);
    searchDebounceTimer->setSingleShot(true);
    searchDebounceTimer->setInterval(200);
    connect(searchDebounceTimer, &QTimer::timeout, this, &ChatView::updateSearchMatches);
    connect(searchEdit, &QLineEdit::textChanged, this, [this]() { searchDebounceTimer->start(); });
    connect(searchEdit, &QLineEdit::returnPressed, this, [this]() { moveToSearchMatch(1); });

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ChatView::onScrollBarValueChanged);

    // Jump-to-bottom button
    jumpToBottomButton = new QPushButton(QChar(0x2193), this);
    jumpToBottomButton->setFixedSize(32, 32);
    jumpToBottomButton->setCursor(Qt::PointingHandCursor);
    jumpToBottomButton->setToolTip(tr("Jump to bottom"));
    jumpToBottomButton->setStyleSheet(
        QStringLiteral("QPushButton {"
                       "  background-color: palette(window);"
                       "  border: 1px solid palette(mid);"
                       "  border-radius: 16px;"
                       "  font-size: 16px;"
                       "  color: palette(text);"
                       "}"
                       "QPushButton:hover {"
                       "  background-color: palette(highlight);"
                       "  color: palette(highlighted-text);"
                       "  border-color: palette(highlight);"
                       "}"));
    jumpToBottomButton->setGraphicsEffect(new QGraphicsOpacityEffect(jumpToBottomButton));
    auto *effect = qobject_cast<QGraphicsOpacityEffect *>(jumpToBottomButton->graphicsEffect());
    if (effect)
        effect->setOpacity(0.0);

    jumpToBottomAnimation = new QPropertyAnimation(effect, "opacity", this);
    jumpToBottomAnimation->setDuration(Core::AnimationConfig::instance().scaled(140));
    jumpToBottomAnimation->setEasingCurve(QEasingCurve::InOutQuad);

    scrollAnimation = new QPropertyAnimation(verticalScrollBar(), "value", this);
    scrollAnimation->setDuration(Core::AnimationConfig::instance().scaled(140));
    scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // Empty/loading placeholder, centered over the viewport. Shown while a
    // channel's history is being fetched and when a channel has no messages.
    placeholderLabel = new QLabel(viewport());
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet(
            QStringLiteral("QLabel { color: palette(mid); font-size: 13px; }"));
    placeholderLabel->setVisible(false);

    jumpToBottomButton->hide();
    connect(jumpToBottomButton, &QPushButton::clicked, this, [this]() {
        animateScrollToBottom();
        jumpToBottomButton->hide();
    });
}

void ChatView::animateScrollToBottom()
{
    QScrollBar *vbar = verticalScrollBar();
    if (scrollAnimation && vbar) {
        scrollAnimation->stop();
        scrollAnimation->setStartValue(vbar->value());
        scrollAnimation->setEndValue(vbar->maximum());
        scrollAnimation->start();
    } else if (vbar) {
        vbar->setValue(vbar->maximum());
    }
}

bool ChatView::hasTextSelection() const
{
    return selectionAnchor.isValid() && selectionHead.isValid() && selectionAnchor != selectionHead;
}

ChatCursor ChatView::selectionStart() const
{
    return (selectionAnchor < selectionHead) ? selectionAnchor : selectionHead;
}

ChatCursor ChatView::selectionEnd() const
{
    return (selectionAnchor < selectionHead) ? selectionHead : selectionAnchor;
}

void ChatView::setModel(QAbstractItemModel *model)
{
    for (const auto &connection : modelConnections)
        disconnect(connection);
    modelConnections.clear();

    QListView::setModel(model);
    if (!model)
        return;

    modelConnections.append(connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        isFetchingTop = false;
        anchorIndex = QPersistentModelIndex();
        hoverLayoutRow = -1;
        cancelPendingJump();
        appearRows.clear();
        if (appearAnimation)
            appearAnimation->stop();
        // A channel switch resets the model; start from a clean not-at-bottom
        // state so history top-insertion never re-glides, then settle to the
        // bottom smoothly.
        atBottom = false;
        QTimer::singleShot(0, this, &ChatView::animateScrollToBottom);
        // A reset with no rows means a channel switch with a fetch in flight
        // (requestLoadChannel runs right after); say so instead of showing a
        // blank view that looks like an empty channel.
        if (this->model() && this->model()->rowCount() == 0)
            showLoadingPlaceholder();
    }));

    modelConnections.append(connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this,
                                    [this]() { hoverLayoutRow = -1; }));
    modelConnections.append(connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this,
                                    &ChatView::onRowsAboutToBeInserted));
    modelConnections.append(connect(model, &QAbstractItemModel::rowsInserted, this,
                                    &ChatView::onRowsInserted));
    modelConnections.append(connect(model, &QAbstractItemModel::rowsInserted, this,
                                    [this]() { hidePlaceholder(); }));

    // Watch for image/embed loading completion to update the view
    modelConnections.append(connect(model, &QAbstractItemModel::dataChanged, this,
                                    [this](const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                           const QVector<int> &roles) {
        // Content may have changed under the cursor — drop the hover cache.
        hoverLayoutRow = -1;
        if (roles.contains(ChatModel::AttachmentsRole) ||
            roles.contains(ChatModel::EmbedsRole)) {
            // Repaint only the affected rows (GIF frame ticks, upload progress,
            // embed settle) instead of the whole viewport.
            const QRect dirty = visualRect(topLeft).united(visualRect(bottomRight));
            if (!dirty.isEmpty())
                viewport()->update(dirty);
            else
                viewport()->update();
        }
    }));
}

void ChatView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        QModelIndex idx = indexAt(pos);
        int charPos = ChatLayout::hitTestCharIndex(this, idx, pos);

        if (charPos >= 0) {
            selectionAnchor = { idx.row(), charPos };
            selectionHead = selectionAnchor;
            viewport()->update();
        } else {
            clearSelection();
        }
    }
    QListView::mousePressEvent(event);
}

void ChatView::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    QModelIndex idx = indexAt(pos);

    bool inSelectionDrag = (event->buttons() & Qt::LeftButton) && selectionAnchor.isValid();
    if (inSelectionDrag) {
        if (!model())
            return;
        int currentRow = idx.isValid() ? idx.row() : (model()->rowCount() - 1);
        if (currentRow < 0)
            return;

        if (!idx.isValid())
            idx = model()->index(currentRow, 0);
    }

    // Reuse the resolved layout while the hovered row and scroll offset are
    // unchanged — resolveLayout deep-copies the row's role payloads, which is
    // far too expensive to redo for every mouse-move event within one row.
    const int hoverScrollValue = verticalScrollBar()->value();
    const int hoverRow = idx.isValid() ? idx.row() : -1;
    if (hoverRow != hoverLayoutRow || hoverScrollValue != hoverLayoutScroll) {
        hoverLayoutCache = ChatLayout::resolveLayout(this, idx);
        hoverLayoutRow = hoverRow;
        hoverLayoutScroll = hoverScrollValue;
    }
    const ChatLayout::ResolvedLayout &resolved = hoverLayoutCache;

    if (inSelectionDrag) {
        const QRect &textRect = resolved.layout.textRect;

        int newChar = -1;

        if (pos.y() < textRect.top()) {
            newChar = 0;
        } else if (pos.y() > textRect.bottom()) {
            QString content = idx.data(ChatModel::ContentRole).toString();
            newChar = content.length();
        } else {
            if (pos.x() < textRect.left()) {
                newChar = 0;
            } else if (pos.x() > textRect.right()) {
                QString content = idx.data(ChatModel::ContentRole).toString();
                newChar = content.length();
            } else {
                newChar = ChatLayout::hitTestCharIndex(resolved, pos);
            }
        }

        if (newChar >= 0) {
            selectionHead = { idx.row(), newChar };
            viewport()->update();
        }
    }

    auto region = ChatLayout::hitTest(resolved, pos);

    Qt::CursorShape shape = Qt::ArrowCursor;
    int charPos = -1;
    if (region) {
        if (region->kind == ChatLayout::HitRegion::Kind::TextCursor) {
            shape = Qt::IBeamCursor;
            charPos = ChatLayout::hitTestCharIndex(resolved, pos);
        } else {
            shape = Qt::PointingHandCursor;
        }
    }
    // The quick-reaction bar is clickable, so show a hand cursor over it even
    // when it floats above plain text.
    if (idx.isValid() && resolved.layout.quickReaction.barRect.contains(pos))
        shape = Qt::PointingHandCursor;
    if (viewport()->cursor().shape() != shape)
        viewport()->setCursor(shape);

    if (hoveredRow != idx.row() || hoveredChar != charPos) {
        if (hoveredRow != -1)
            update(visualRect(model()->index(hoveredRow, 0)));
        hoveredRow = idx.row();
        hoveredChar = charPos;
        if (hoveredRow != -1)
            update(visualRect(idx));
    }

    QListView::mouseMoveEvent(event);
}

void ChatView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QListView::mouseReleaseEvent(event);
        return;
    }

    QPoint pos = event->pos();
    QModelIndex idx = indexAt(pos);
    ChatLayout::ResolvedLayout resolved = ChatLayout::resolveLayout(this, idx);

    // Quick-reaction hover bar: clicking a quick emoji toggles that reaction;
    // clicking "more" opens the full picker. Handled before the hit-test switch
    // so it also works over empty space and never starts a text selection.
    if (idx.isValid() && hoveredRow == idx.row() &&
        resolved.layout.quickReaction.barRect.contains(pos)) {
        handleQuickReactionClick(idx, resolved, pos);
        return;
    }

    auto region = ChatLayout::hitTest(resolved, pos);

    if (!region) {
        QListView::mouseReleaseEvent(event);
        return;
    }

    using Kind = ChatLayout::HitRegion::Kind;

    auto openExternalLink = [this](const QString &url) {
        if (url.isEmpty())
            return;
        ConfirmPopup dialog(tr("External Link"),
                            QString(tr("Are you sure you want to open <b>%1</b>?")).arg(url),
                            tr("Open Link"), this);
        if (dialog.exec() == QDialog::Accepted)
            QDesktopServices::openUrl(QUrl(url));
    };

    auto openImage = [this](const QUrl &url, const QPixmap &pixmap) {
        auto *viewer = new ImageViewer(window());
        viewer->showImage(url, pixmap);
    };

    auto openVideo = [this](const QUrl &url) {
        if (!url.isValid() || url.isEmpty())
            return;
        auto *dialog = new VideoPlayerDialog(url, window());
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    };

    switch (region->kind) {
    case Kind::Reaction: {
        if (hasTextSelection())
            break;
        auto *chatModel = qobject_cast<ChatModel *>(model());
        if (!chatModel || region->index < 0 || region->index >= resolved.ctx.reactions.size())
            break;
        Snowflake channelId = chatModel->getActiveChannelId();
        Snowflake messageId = idx.data(ChatModel::MessageIdRole).toULongLong();
        const ReactionData &r = resolved.ctx.reactions[region->index];
        QString emojiStr = r.emojiId.isValid() ? (r.emojiName + ":" + QString::number(r.emojiId))
                                               : r.emojiName;
        emit toggleReactionClicked(channelId, messageId, emojiStr, r.me, r.isBurst);
        break;
    }

    case Kind::PollVote: {
        if (hasTextSelection())
            break;
        auto *chatModel = qobject_cast<ChatModel *>(model());
        if (!chatModel || region->index < 0 || region->index >= resolved.ctx.poll.answers.size())
            break;
        const Snowflake channelId = chatModel->getActiveChannelId();
        const Snowflake messageId = idx.data(ChatModel::MessageIdRole).toULongLong();
        emit chatModel->pollVoteRequested(channelId, messageId,
                                          { resolved.ctx.poll.answers[region->index].id });
        break;
    }

    case Kind::AttachmentImage:
    case Kind::AttachmentFile:
    case Kind::AttachmentVideo: {
        if (region->index < 0 || region->index >= resolved.ctx.attachments.size())
            break;
        const AttachmentData &att = resolved.ctx.attachments[region->index];

        // Video attachment: play it in the in-app player.
        if (att.isVideo) {
            QUrl playUrl = att.proxyUrl.isValid() ? att.proxyUrl : att.originalUrl;
            openVideo(playUrl);
            break;
        }

        // Voice message: toggle audio playback and surface the mini-player so
        // the user can seek/pause/change volume. Audio never autoplays — the
        // click is always required.
        if (att.isVoice) {
            ensureVoicePlayerPanel();
            auto *vp = voicePlayer();
            if (vp->playbackState() == QMediaPlayer::PlayingState &&
                vp->source() == att.proxyUrl) {
                vp->pause();
            } else {
                vp->setSource(att.proxyUrl);
                vp->play();
            }
            break;
        }

        if (att.isImage) {
            if (att.isSpoiler) {
                auto *chatModel = qobject_cast<ChatModel *>(model());
                if (chatModel && !chatModel->isSpoilerRevealed(att.id)) {
                    chatModel->revealSpoiler(att.id);
                    break;
                }
            }
            // GIF: toggle animation on click instead of opening viewer
            if (att.isGif) {
                auto *chatModel = qobject_cast<ChatModel *>(model());
                if (chatModel) {
                    auto *anim = chatModel->ensureGifAnimation(att.proxyUrl, idx.row());
                    if (anim && anim->isReady())
                        anim->toggle();
                }
                break;
            }
            openImage(att.proxyUrl, att.pixmap);
        } else {
            ConfirmPopup dialog(tr("Open File"),
                                QString(tr("Do you want to open <b>%1</b> (%2) in your browser?"))
                                        .arg(att.filename)
                                        .arg(ChatLayout::formatFileSize(att.fileSizeBytes)),
                                tr("Open"), this);
            if (dialog.exec() == QDialog::Accepted)
                QDesktopServices::openUrl(att.originalUrl);
        }
        break;
    }

    case Kind::EmbedThumbnail: {
        if (region->index < 0 || region->index >= resolved.ctx.embeds.size())
            break;
        const auto &embed = resolved.ctx.embeds[region->index];
        if (!embed.thumbnail.isNull())
            openImage(QUrl(region->url), embed.thumbnail);
        else
            openExternalLink(region->url);
        break;
    }

    case Kind::EmbedImage: {
        if (region->index < 0 || region->index >= resolved.ctx.embeds.size())
            break;
        const auto &embed = resolved.ctx.embeds[region->index];
        if (region->subIndex < 0 || region->subIndex >= embed.images.size())
            break;
        const auto &img = embed.images[region->subIndex];
        openImage(img.url, img.pixmap);
        break;
    }

    case Kind::EmbedVideoThumbnail:
        // Play the video embed in the in-app player (region->url is the media URL).
        openVideo(QUrl(region->url));
        break;
    case Kind::EmbedAuthor:
    case Kind::EmbedTitle:
    case Kind::EmbedLink:
        openExternalLink(region->url);
        break;

    case Kind::TextLink:
        if (region->url.startsWith("acheron://channel/")) {
            bool ok = false;
            quint64 id = region->url.mid(18).toULongLong(&ok);
            if (ok)
                emit channelMentionClicked(Core::Snowflake(id));
        } else {
            openExternalLink(region->url);
        }
        break;

    case Kind::TextCursor:
    case Kind::Avatar:
    case Kind::UsernameHeader:
    case Kind::ReplyBar:
    case Kind::EmbedDescription:
    case Kind::EmbedFieldName:
    case Kind::EmbedFieldValue:
        break;
    }

    QListView::mouseReleaseEvent(event);
}

void ChatView::clearSelection()
{
    if (selectionAnchor.isValid()) {
        selectionAnchor = { -1, -1 };
        selectionHead = { -1, -1 };
        viewport()->update();
    }
}

void ChatView::leaveEvent(QEvent *event)
{
    bool needsUpdate = (hoveredRow != -1);
    hoveredRow = -1;
    hoveredChar = -1;
    hoverLayoutRow = -1;

    if (needsUpdate) {
        viewport()->update();
    }

    viewport()->unsetCursor();
    QListView::leaveEvent(event);
}


void ChatView::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    updatePlaceholderPosition();
    if (jumpToBottomButton) {
        int margin = 12;
        jumpToBottomButton->move(width() - jumpToBottomButton->width() - margin,
                                 height() - jumpToBottomButton->height() - margin);
    }
    positionSearchPanel();
    positionVoicePlayerPanel();
}

bool ChatView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        QModelIndex idx = indexAt(helpEvent->pos());

        QDateTime editedTime = idx.data(ChatModel::EditedTimestampRole).toDateTime();
        if (editedTime.isValid()) {
            ChatLayout::ResolvedLayout resolved = ChatLayout::resolveLayout(this, idx);
            auto markerRect = ChatLayout::editedMarkerRectAt(resolved, helpEvent->pos());
            if (markerRect) {
                QToolTip::showText(helpEvent->globalPos(),
                                   tr("Edited %1").arg(Core::TimeUtils::absoluteTime(editedTime)),
                                   viewport(), *markerRect);
                return true;
            }
        }
    }

    return QListView::viewportEvent(event);
}

void ChatView::showLoadingPlaceholder(const QString &text)
{
    placeholderLabel->setText(text.isEmpty() ? tr("Loading messages…") : text);
    updatePlaceholderPosition();
    placeholderLabel->setVisible(true);
}

void ChatView::showEmptyPlaceholder(const QString &text)
{
    placeholderLabel->setText(text.isEmpty() ? tr("No messages here yet") : text);
    updatePlaceholderPosition();
    placeholderLabel->setVisible(true);
}

void ChatView::hidePlaceholder()
{
    placeholderLabel->setVisible(false);
}

void ChatView::updatePlaceholderPosition()
{
    if (placeholderLabel)
        placeholderLabel->setGeometry(viewport()->rect());
}

void ChatView::onHistoryRequestFinished()
{
    isFetchingTop = false;
    if (model() && model()->rowCount() == 0)
        showEmptyPlaceholder();
}

void ChatView::onRowsAboutToBeInserted(const QModelIndex &parent, int start, int end)
{
    // Row indices shift on insertion — the hover layout cache is stale.
    hoverLayoutRow = -1;

    // History pagination inserts at the top: row indices of any rows still
    // fading in would be wrong, so drop the fade instead of misplacing it.
    if (!parent.isValid() && start == 0) {
        appearRows.clear();
        if (appearAnimation)
            appearAnimation->stop();
    }

    QScrollBar *vbar = verticalScrollBar();
    atBottom = (vbar->value() + vbar->pageStep() >= vbar->maximum());

    if (start == 0) {
        QPoint topPoint(5, 5);
        QModelIndex topVisible = indexAt(topPoint);

        if (topVisible.isValid()) {
            anchorIndex = QPersistentModelIndex(topVisible);
            anchorDistanceFromBottom = visualRect(topVisible).bottom();
        }
    }
}

bool ChatView::scrollToMessage(Core::Snowflake messageId)
{
    auto *chatModel = qobject_cast<ChatModel *>(model());
    if (!chatModel)
        return false;

    if (!messageId.isValid())
        return false;

    const int row = chatModel->rowForMessage(messageId);
    if (row >= 0) {
        cancelPendingJump();
        atBottom = false;
        const QModelIndex target = chatModel->index(row, 0);
        scrollTo(target, QAbstractItemView::PositionAtCenter);
        setCurrentIndex(target);
        return true;
    }

    if (!messageManager)
        return false;

    if (pendingJumpMessageId == messageId)
        return true; // already fetching

    pendingJumpMessageId = messageId;
    emit messageJumpLoadStarted();
    showLoadingPlaceholder(tr("Loading message…"));
    messageManager->requestMessage(chatModel->getActiveChannelId(), messageId);
    return false;
}

void ChatView::cancelPendingJump()
{
    if (!pendingJumpMessageId.isValid())
        return;

    pendingJumpMessageId = Core::Snowflake::Invalid;
    emit messageJumpLoadFinished(false);
}

void ChatView::onMessageJumpReady(Core::Snowflake channelId, Core::Snowflake messageId)
{
    Q_UNUSED(channelId);
    if (pendingJumpMessageId != messageId)
        return;

    QTimer::singleShot(0, this, [this, messageId]() {
        if (pendingJumpMessageId != messageId)
            return;

        auto *chatModel = qobject_cast<ChatModel *>(model());
        if (!chatModel) {
            pendingJumpMessageId = Core::Snowflake::Invalid;
            emit messageJumpLoadFinished(false);
            return;
        }

        const int row = chatModel->rowForMessage(messageId);
        if (row >= 0) {
            atBottom = false;
            const QModelIndex target = chatModel->index(row, 0);
            scrollTo(target, QAbstractItemView::PositionAtCenter);
            setCurrentIndex(target);
            hidePlaceholder();
            emit messageJumpLoadFinished(true);
        } else {
            showEmptyPlaceholder(tr("Message not found"));
            emit messageJumpLoadFinished(false);
        }
        pendingJumpMessageId = Core::Snowflake::Invalid;
    });
}

void ChatView::onMessageJumpFailed(Core::Snowflake channelId, Core::Snowflake messageId)
{
    Q_UNUSED(channelId);
    if (pendingJumpMessageId != messageId)
        return;

    pendingJumpMessageId = Core::Snowflake::Invalid;
    showEmptyPlaceholder(tr("Message not found"));
    emit messageJumpLoadFinished(false);
}

void ChatView::onRowsInserted(const QModelIndex &parent, int start, int end)
{
    // New messages arriving at the bottom fade in. History pagination inserts
    // at the top (start == 0) and must not animate.
    if (atBottom && !parent.isValid() && start > 0 && model()) {
        if (!appearAnimation) {
            appearAnimation = new QVariantAnimation(this);
            appearAnimation->setStartValue(0.0);
            appearAnimation->setEndValue(1.0);
            appearAnimation->setDuration(Core::AnimationUtils::duration(300));
            appearAnimation->setEasingCurve(QEasingCurve::OutCubic);
            connect(appearAnimation, &QVariantAnimation::valueChanged, this,
                    [this](const QVariant &v) { onAppearTick(v.toReal()); });
        }
        for (int row = start; row <= end; ++row)
            appearRows.insert(row, 0.0);
        appearAnimation->stop();
        appearAnimation->start();
    }

    if (atBottom) {
        animateScrollToBottom();
    } else if (start == 0 && anchorIndex.isValid()) {
        if (!pendingScroll_) {
            pendingScroll_ = true;
            setUpdatesEnabled(false);
        }

        QTimer::singleShot(0, this, [this]() {
            if (!pendingScroll_)
                return;
            pendingScroll_ = false;
            if (!anchorIndex.isValid()) {
                setUpdatesEnabled(true);
                return;
            }

            int originalScrollValue = verticalScrollBar()->value();
            scrollTo(anchorIndex, QAbstractItemView::PositionAtTop);
            QRect newRect = visualRect(anchorIndex);
            if (!newRect.isValid()) {
                verticalScrollBar()->setValue(originalScrollValue);
                anchorIndex = QPersistentModelIndex();
                setUpdatesEnabled(true);
                return;
            }
            int diff = newRect.bottom() - anchorDistanceFromBottom;
            verticalScrollBar()->setValue(verticalScrollBar()->value() + diff);

            anchorIndex = QPersistentModelIndex();
            setUpdatesEnabled(true);
        });
    }
}

void ChatView::onScrollBarValueChanged(int value)
{
    QScrollBar *vbar = verticalScrollBar();
    bool atBottomNow = (vbar->maximum() - value <= 200);

    if (jumpToBottomButton && jumpToBottomAnimation) {
        const bool shouldShow = !atBottomNow;
        if (jumpToBottomButton->isVisible() != shouldShow) {
            jumpToBottomAnimation->stop();
            jumpToBottomAnimation->setStartValue(static_cast<QGraphicsOpacityEffect *>(jumpToBottomButton->graphicsEffect())->opacity());
            jumpToBottomAnimation->setEndValue(shouldShow ? 1.0 : 0.0);
            if (shouldShow)
                jumpToBottomButton->show();
            disconnect(jumpToBottomAnimation, &QPropertyAnimation::finished, this, nullptr);
            connect(jumpToBottomAnimation, &QPropertyAnimation::finished, this, [this, shouldShow]() {
                if (!shouldShow)
                    jumpToBottomButton->hide();
            }, Qt::SingleShotConnection);
            jumpToBottomAnimation->start();
        }
    }

    if (value < 200 && !isFetchingTop) {
        isFetchingTop = true;
        emit historyRequested();
    }
}

void ChatView::onHistoryRequestFailed()
{
    isFetchingTop = false;
    if (model() && model()->rowCount() == 0)
        showEmptyPlaceholder();
}

void ChatView::beginChannelCrossfade()
{
    // Tear down any in-flight crossfade first so overlays never stack.
    if (channelFadeAnimation) {
        channelFadeAnimation->stop();
        channelFadeAnimation->deleteLater();
        channelFadeAnimation = nullptr;
    }
    if (channelFadeOverlay) {
        channelFadeOverlay->deleteLater();
        channelFadeOverlay = nullptr;
    }

    if (!viewport())
        return;

    // Capture the current (old-channel) content before the model is reset.
    const QPixmap snapshot = viewport()->grab();
    if (snapshot.isNull())
        return;

    auto *overlay = new ChannelFadeOverlay(snapshot, viewport());
    overlay->setGeometry(viewport()->rect());
    overlay->show();
    overlay->raise();
    channelFadeOverlay = overlay;

    auto *animation = new QVariantAnimation(this);
    animation->setStartValue(1.0);
    animation->setEndValue(0.0);
    animation->setDuration(Core::AnimationConfig::instance().scaled(100));
    animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(animation, &QVariantAnimation::valueChanged, this,
            [overlay](const QVariant &value) { overlay->setOpacity(value.toReal()); });
    connect(animation, &QVariantAnimation::finished, this, [this, overlay]() {
        overlay->deleteLater();
        if (channelFadeOverlay == overlay)
            channelFadeOverlay = nullptr;
        if (channelFadeAnimation) {
            channelFadeAnimation->deleteLater();
            channelFadeAnimation = nullptr;
        }
    });

    channelFadeAnimation = animation;
    animation->start();
}

void ChatView::setCurrentUserId(Core::Snowflake userId)
{
    currentUserId = userId;
}

void ChatView::setMessageManager(Core::MessageManager *mgr)
{
    if (messageManager) {
        disconnect(messageJumpReadyConnection);
        disconnect(messageJumpFailedConnection);
    }

    cancelPendingJump();
    messageManager = mgr;

    if (messageManager) {
        messageJumpReadyConnection = connect(messageManager, &Core::MessageManager::messageJumpReady,
                                             this, &ChatView::onMessageJumpReady);
        messageJumpFailedConnection = connect(messageManager, &Core::MessageManager::messageJumpFailed,
                                              this, &ChatView::onMessageJumpFailed);
    }
}

void ChatView::setCanPinMessages(bool canPin)
{
    canPinMessages = canPin;
}

void ChatView::setCanManageMessages(bool canManage)
{
    canManageMessages = canManage;
}

void ChatView::setCompactMode(bool compact)
{
    if (isCompactMode == compact)
        return;

    isCompactMode = compact;
    setProperty("compactMode", compact);

    if (model()) {
        for (int row = 0; row < model()->rowCount(); ++row)
            model()->setData(model()->index(row, 0), QSize(), ChatModel::CachedSizeRole);
    }

    scheduleDelayedItemsLayout();
    viewport()->update();
}

void ChatView::setShowTimestamps(bool enabled)
{
    if (isShowTimestamps == enabled)
        return;

    isShowTimestamps = enabled;
    viewport()->update();
}

void ChatView::setGuildOrder(const QStringList &orderedGuildIds)
{
    cachedGuildOrder = orderedGuildIds;
}

void ChatView::setCurrentGuildId(const Core::Snowflake &guildId)
{
    cachedGuildId = guildId;
}

void ChatView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());
    if (!index.isValid())
        return;

    auto *chatModel = qobject_cast<ChatModel *>(model());
    if (!chatModel)
        return;

    Core::Snowflake messageId = index.data(ChatModel::MessageIdRole).toULongLong();
    Core::Snowflake authorId = index.data(ChatModel::UserIdRole).toULongLong();
    Core::Snowflake channelId = chatModel->getActiveChannelId();
    bool isOwnMessage = (authorId == currentUserId);
    Snowflake guildId = chatModel->getActiveGuildId();
    QUrl messageLink(QStringLiteral("https://discord.com/channels/%1/%2/%3")
                             .arg(guildId.isValid() ? QString::number(static_cast<qulonglong>(guildId))
                                                    : QStringLiteral("@me"),
                                  QString::number(static_cast<qulonglong>(channelId)),
                                  QString::number(static_cast<qulonglong>(messageId))));

    ChatLayout::ResolvedLayout resolved = ChatLayout::resolveLayout(this, index);
    auto region = ChatLayout::hitTest(resolved, event->pos());
    if (region && (region->kind == ChatLayout::HitRegion::Kind::Avatar || region->kind == ChatLayout::HitRegion::Kind::UsernameHeader)) {
        emit userContextMenuRequested(authorId, event->globalPos());
        return;
    }

    QMenu menu(this);
    auto copyTextToClipboard = [](const QString &text) {
        QGuiApplication::clipboard()->setText(text);
    };

    // Section 1: Message actions — Edit, Delete, Reply, Pin
    if (isOwnMessage) {
        QAction *editAction = menu.addAction(tr("Edit Message"));
        connect(editAction, &QAction::triggered, this, [this, index]() {
            startInlineEdit(index);
        });
    }

    if (isOwnMessage || canManageMessages) {
        QAction *deleteAction = menu.addAction(tr("Delete Message"));
        connect(deleteAction, &QAction::triggered, this, [this, channelId, messageId]() {
            emit deleteMessageRequested(channelId, messageId);
        });
    }

    QAction *replyAction = menu.addAction(tr("Reply"));
    connect(replyAction, &QAction::triggered, this, [this, channelId, messageId]() {
        emit replyToMessageRequested(channelId, messageId);
    });

    QAction *forwardAction = menu.addAction(tr("Forward"));
    const bool isPendingRow = index.data(ChatModel::IsPendingRole).toBool();
    forwardAction->setEnabled(!isPendingRow);
    connect(forwardAction, &QAction::triggered, this, [this, channelId, messageId]() {
        emit forwardMessageRequested(channelId, messageId);
    });

    if (canPinMessages) {
        QAction *pinAction = menu.addAction(tr("Pin Message"));
        connect(pinAction, &QAction::triggered, this, [this, channelId, messageId]() {
            emit pinMessageRequested(channelId, messageId);
        });
    }

    menu.addSeparator();

    QAction *pinnedMessagesAction = menu.addAction(tr("Pinned Messages"));
    connect(pinnedMessagesAction, &QAction::triggered, this, [this, channelId]() {
        emit pinnedMessagesRequested(channelId);
    });

    // Section 2: Utility — Copy Text, Copy ID, Copy Link, Add Reaction
    QAction *copyAction = menu.addAction(tr("Copy Text"));
    copyAction->setShortcut(QKeySequence::Copy);
    if (hasTextSelection()) {
        connect(copyAction, &QAction::triggered, this, [this]() {
            copySelectedText();
        });
    } else {
        connect(copyAction, &QAction::triggered, this, [this, index]() {
            copyMessageContent(index);
        });
    }

    if (Core::isDeveloperModeEnabled()) {
        QAction *copyIdAction = menu.addAction(tr("Copy ID"));
        connect(copyIdAction, &QAction::triggered, this, [copyTextToClipboard, messageId]() {
            copyTextToClipboard(QString::number(static_cast<qulonglong>(messageId)));
        });
    }

    QAction *copyLinkAction = menu.addAction(tr("Copy Link"));
    connect(copyLinkAction, &QAction::triggered, this, [copyTextToClipboard, messageLink]() {
        copyTextToClipboard(messageLink.toString());
    });

    QAction *reactAction = menu.addAction(tr("Add Reaction"));
    connect(reactAction, &QAction::triggered, this, [this, channelId, messageId]() {
        openReactionPicker(channelId, messageId);
    });
    // Section 3: Attachment actions (contextual — only shown when clicking an attachment)
    if (region && (region->kind == ChatLayout::HitRegion::Kind::AttachmentImage ||
                   region->kind == ChatLayout::HitRegion::Kind::AttachmentFile ||
                   region->kind == ChatLayout::HitRegion::Kind::AttachmentVideo)) {
        if (region->index >= 0 && region->index < resolved.ctx.attachments.size()) {
            const AttachmentData &att = resolved.ctx.attachments[region->index];
            menu.addSeparator();

            // "Open in Gallery" gathers the message's image attachments so the
            // user can navigate between them, opening on the clicked image.
            if (att.isImage) {
                QList<AttachmentData> galleryAttachments;
                int galleryIndex = -1;
                for (int i = 0; i < resolved.ctx.attachments.size(); ++i) {
                    const AttachmentData &candidate = resolved.ctx.attachments[i];
                    if (!candidate.isImage)
                        continue;
                    if (i == region->index)
                        galleryIndex = galleryAttachments.size();
                    galleryAttachments.append(candidate);
                }
                QAction *galleryAction = menu.addAction(tr("Open in Gallery"));
                connect(galleryAction, &QAction::triggered, this,
                        [this, galleryAttachments, galleryIndex]() {
                            auto *gallery = new AttachmentGallery(galleryAttachments,
                                                                  galleryIndex, window());
                            gallery->show();
                        });
            }

            QAction *saveAsAction = menu.addAction(tr("Save As"));
            connect(saveAsAction, &QAction::triggered, this, [this, att]() {
                AttachmentGallery::saveAttachment(att, this);
            });

            QAction *copyAttachmentLinkAction = menu.addAction(tr("Copy Link"));
            connect(copyAttachmentLinkAction, &QAction::triggered, this,
                    [copyTextToClipboard, att]() {
                        copyTextToClipboard(att.originalUrl.toString());
                    });

            QAction *openBrowserAction = menu.addAction(tr("Open in Browser"));
            connect(openBrowserAction, &QAction::triggered, this, [att]() {
                QDesktopServices::openUrl(att.originalUrl);
            });
        }
    }

    // Section 4: Pending upload cancel
    bool isPending = index.data(ChatModel::IsPendingRole).toBool();
    bool hasAttachments = !index.data(ChatModel::AttachmentsRole).isNull();
    if (isOwnMessage && isPending && hasAttachments) {
        menu.addSeparator();
        QAction *cancelAction = menu.addAction(tr("Cancel Upload"));
        connect(cancelAction, &QAction::triggered, this, [this, channelId, messageId]() {
            emit cancelUploadRequested(channelId, messageId);
        });
    }

    menu.exec(event->globalPos());
}

void ChatView::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Find)) {
        showSearchBar();
        return;
    }
    if (event->matches(QKeySequence::Copy)) {
        copySelectedText();
        return;
    }
    QListView::keyPressEvent(event);
}

static bool hasLocalFiles(const QMimeData *mime)
{
    if (!mime->hasUrls())
        return false;
    const auto urls = mime->urls();
    return std::any_of(urls.begin(), urls.end(),
                       [](const QUrl &url) { return url.isLocalFile(); });
}

void ChatView::dragEnterEvent(QDragEnterEvent *event)
{
    if (hasLocalFiles(event->mimeData()))
        event->acceptProposedAction();
}

void ChatView::dragMoveEvent(QDragMoveEvent *event)
{
    if (hasLocalFiles(event->mimeData()))
        event->acceptProposedAction();
}

void ChatView::dropEvent(QDropEvent *event)
{
    if (!hasLocalFiles(event->mimeData()))
        return;
    event->acceptProposedAction();
    emit filesDropped(event->mimeData()->urls());
}

void ChatView::copySelectedText()
{
    if (!hasTextSelection())
        return;

    ChatCursor start = selectionStart();
    ChatCursor end = selectionEnd();

    QString selectedText;
    static const QRegularExpression imgAltRe(
            R"re(<img[^>]*alt="([^"]*)"[^>]*>)re", QRegularExpression::CaseInsensitiveOption);

    for (int row = start.row; row <= end.row; row++) {
        QModelIndex idx = model()->index(row, 0);
        QString html = idx.data(ChatModel::HtmlRole).toString();

        // Reuse the doc the layout layer cached for this row: the selection
        // indices come from ChatLayout::hitTestCharIndex measured against that
        // same cached doc, so positions align exactly and we skip re-parsing
        // the HTML. Fall back to a local bare doc when the row was never laid
        // out (identical to the previous always-bare behavior).
        QTextDocument *doc = nullptr;
        QTextDocument localDoc;
        auto *chatModel = qobject_cast<ChatModel *>(model());
        if (chatModel) {
            const Core::Snowflake msgId(idx.data(ChatModel::MessageIdRole).toULongLong());
            doc = chatModel->getCachedDocument(bodyDocKey(msgId));
        }
        if (!doc) {
            localDoc.setHtml(html);
            doc = &localDoc;
        }

        int docLength = doc->characterCount() - 1;
        int startChar = (row == start.row) ? start.index : 0;
        int endChar = (row == end.row) ? end.index : docLength;

        startChar = qBound(0, startChar, docLength);
        endChar = qBound(0, endChar, docLength);

        if (startChar >= endChar && row == start.row && row == end.row)
            continue;

        // Custom emoji are rendered as inline images (one object-replacement
        // char each). Pull their alt text (":name:") out of the HTML so copying
        // yields readable text instead of U+FFFC placeholders.
        QStringList alts;
        {
            auto it = imgAltRe.globalMatch(html);
            while (it.hasNext())
                alts.append(it.next().captured(1));
        }

        QString rowText;
        {
            int altIdx = 0;
            // One cursor reused across the range instead of allocating a fresh
            // QTextCursor per character.
            QTextCursor c(doc);
            for (int pos = startChar; pos < endChar; ++pos) {
                c.setPosition(pos);
                c.setPosition(pos + 1, QTextCursor::KeepAnchor);
                QString ch = c.selectedText();
                // Object-replacement char == inline image (custom emoji).
                if (ch == QChar(0xFFFC)) {
                    if (altIdx < alts.size())
                        rowText += alts[altIdx];
                    altIdx++;
                } else {
                    if (ch == QChar(0x2029))
                        ch = '\n';
                    rowText += ch;
                }
            }
        }

        if (!selectedText.isEmpty())
            selectedText += '\n';
        selectedText += rowText;
    }

    if (!selectedText.isEmpty())
        QGuiApplication::clipboard()->setText(selectedText);
}

void ChatView::copyMessageContent(const QModelIndex &index)
{
    QString content = index.data(ChatModel::ContentRole).toString();
    if (!content.isEmpty())
        QGuiApplication::clipboard()->setText(content);
}

void ChatView::startInlineEdit(const QModelIndex &index)
{
    if (!index.isValid())
        return;

    QString content = index.data(ChatModel::ContentRole).toString();
    Core::Snowflake messageId = index.data(ChatModel::MessageIdRole).toULongLong();

    currentEditingMessageId = messageId;
    currentEditingIndex = index;

    // Invalidate cached size so sizeHint returns the enlarged height
    auto *m = const_cast<QAbstractItemModel *>(index.model());
    m->setData(index, QSize(), ChatModel::CachedSizeRole);

    // Force the view to re-query sizeHint for this row
    scheduleDelayedItemsLayout();

    // Position the edit widget after layout recalculates
    QTimer::singleShot(0, this, [this, content]() {
        if (!currentEditingIndex.isValid())
            return;

        QRect itemRect = visualRect(currentEditingIndex);
        ChatLayout::ResolvedLayout resolved = ChatLayout::resolveLayout(this, currentEditingIndex);
        const QRect &textRect = resolved.layout.textRect;

        int editHeight = qMax(InlineEditMinHeight, itemRect.bottom() - textRect.top() - 4);
        QRect editRect(textRect.left(), textRect.top(), textRect.width(), editHeight);

        inlineEditWidget->setGeometry(editRect);
        inlineEditWidget->setFont(resolved.ctx.font);
        inlineEditWidget->setPlainText(content);
        inlineEditWidget->setVisible(true);
        inlineEditWidget->setFocus();
        inlineEditWidget->selectAll();

        scrollTo(currentEditingIndex, QAbstractItemView::EnsureVisible);
    });
}

void ChatView::commitInlineEdit()
{
    if (!currentEditingIndex.isValid())
        return;

    QString newContent = inlineEditWidget->toPlainText().trimmed();
    QString oldContent = currentEditingIndex.data(ChatModel::ContentRole).toString();

    if (newContent != oldContent && !newContent.isEmpty()) {
        auto *chatModel = qobject_cast<ChatModel *>(model());
        Core::Snowflake channelId = chatModel ? chatModel->getActiveChannelId() : Core::Snowflake::Invalid;
        emit editMessageRequested(channelId, currentEditingMessageId, newContent);
    }

    cancelInlineEdit();
}

void ChatView::cancelInlineEdit()
{
    inlineEditWidget->setVisible(false);

    QModelIndex editedIndex = currentEditingIndex;
    currentEditingMessageId = Core::Snowflake::Invalid;
    currentEditingIndex = QPersistentModelIndex();

    // Invalidate cached size so sizeHint returns the normal height
    if (editedIndex.isValid()) {
        auto *m = const_cast<QAbstractItemModel *>(editedIndex.model());
        m->setData(editedIndex, QSize(), ChatModel::CachedSizeRole);
        scheduleDelayedItemsLayout();
    }
}

bool ChatView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == inlineEditWidget && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            commitInlineEdit();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Escape) {
            cancelInlineEdit();
            return true;
        }
    }
    if (obj == searchEdit && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hideSearchBar();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            moveToSearchMatch(keyEvent->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
    }
    return QListView::eventFilter(obj, event);
}

bool ChatView::isActiveSearchMatchRow(int row) const
{
    return activeSearchMatch >= 0 && activeSearchMatch < searchMatches.size() &&
           searchMatches[activeSearchMatch] == row;
}

qreal ChatView::rowAppearOpacity(int row) const
{
    auto it = appearRows.constFind(row);
    if (it == appearRows.constEnd())
        return 1.0;
    return it.value();
}

void ChatView::onAppearTick(qreal progress)
{
    for (auto &opacity : appearRows)
        opacity = progress;
    viewport()->update();
    if (progress >= 1.0)
        appearRows.clear();
}

void ChatView::showSearchBar()
{
    if (!searchPanel)
        return;

    searchPanel->show();
    Core::AnimationUtils::fadeIn(searchPanel, 120);
    positionSearchPanel();
    searchEdit->setFocus();
    searchEdit->selectAll();
    updateSearchMatches();
}

void ChatView::hideSearchBar()
{
    if (!searchPanel)
        return;

    searchPanel->hide();
    searchMatches.clear();
    activeSearchMatch = -1;
    viewport()->update();
    setFocus();
}

void ChatView::updateSearchMatches()
{
    searchMatches.clear();
    activeSearchMatch = -1;

    const QString query = searchEdit ? searchEdit->text().trimmed() : QString();
    // Early-exit for trivially short queries — matching a single character
    // against the whole history is expensive and never useful.
    if (query.size() >= 2 && model()) {
        for (int row = 0; row < model()->rowCount(); ++row) {
            const QString content = model()->index(row, 0).data(ChatModel::ContentRole).toString();
            if (content.contains(query, Qt::CaseInsensitive))
                searchMatches.append(row);
        }
    }

    if (searchCountLabel) {
        if (query.size() < 2)
            searchCountLabel->clear();
        else if (searchMatches.isEmpty())
            searchCountLabel->setText(tr("0"));
        else
            searchCountLabel->setText(tr("1/%1").arg(searchMatches.size()));
    }

    if (!searchMatches.isEmpty()) {
        activeSearchMatch = 0;
        scrollTo(model()->index(searchMatches[activeSearchMatch], 0),
                 QAbstractItemView::PositionAtCenter);
    }

    viewport()->update();
}

void ChatView::moveToSearchMatch(int delta)
{
    if (searchMatches.isEmpty())
        return;

    activeSearchMatch = (activeSearchMatch + delta + searchMatches.size()) % searchMatches.size();
    if (searchCountLabel)
        searchCountLabel->setText(tr("%1/%2").arg(activeSearchMatch + 1).arg(searchMatches.size()));
    scrollTo(model()->index(searchMatches[activeSearchMatch], 0),
             QAbstractItemView::PositionAtCenter);
    viewport()->update();
}

void ChatView::positionSearchPanel()
{
    if (!searchPanel)
        return;

    const int panelWidth = qMin(360, qMax(220, width() - 32));
    searchPanel->setGeometry(width() - panelWidth - 16, 12, panelWidth, 36);
}

void ChatView::ensureVoicePlayerPanel()
{
    // The panel pointer is only ever assigned VoicePlayerPanel instances below,
    // so a plain static_cast is safe and avoids requiring Q_OBJECT for moc.
    auto *panel = static_cast<VoicePlayerPanel *>(voicePlayerPanel.data());
    if (!panel) {
        panel = new VoicePlayerPanel(this);
        voicePlayerPanel = panel;
    }
    positionVoicePlayerPanel();
    panel->show();
    panel->raise();
}

void ChatView::positionVoicePlayerPanel()
{
    auto *panel = static_cast<VoicePlayerPanel *>(voicePlayerPanel.data());
    if (!panel || !panel->isVisible())
        return;

    panel->adjustSize();
    const int margin = 12;
    const int x = qMax(margin, (width() - panel->width()) / 2);
    const int y = height() - panel->height() - margin;
    panel->move(x, y);
}

void ChatView::openReactionPicker(Core::Snowflake channelId, Core::Snowflake messageId)
{
    EmojiPickerDialog dialog(this);
    dialog.setWindowTitle(tr("Add Reaction"));
    dialog.setSearchPlaceholder(tr("Search emoji"));
    if (!cachedGuildOrder.isEmpty())
        dialog.setOrderedGuildIds(cachedGuildOrder);
    if (cachedGuildId.isValid())
        dialog.setCurrentGuildId(cachedGuildId.toString());
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString emoji = normalizeReactionEmoji(dialog.selectedEmoji());
    if (emoji.isEmpty())
        return;

    emit addReactionRequested(channelId, messageId, emoji);
}

bool ChatView::handleQuickReactionClick(const QModelIndex &index,
                                        const ChatLayout::ResolvedLayout &resolved,
                                        const QPoint &pos)
{
    const auto &bar = resolved.layout.quickReaction;
    if (bar.barRect.isNull() || !bar.barRect.contains(pos))
        return false;

    auto *chatModel = qobject_cast<ChatModel *>(model());
    if (!chatModel)
        return true;

    const Snowflake channelId = chatModel->getActiveChannelId();
    const Snowflake messageId = index.data(ChatModel::MessageIdRole).toULongLong();

    // "More" button opens the full picker and routes the selection back through
    // addReactionRequested, exactly like the context-menu "Add Reaction" action.
    if (bar.moreButtonRect.contains(pos)) {
        openReactionPicker(channelId, messageId);
        return true;
    }

    const QStringList &emojis = ChatLayout::quickReactionEmojis();
    for (int i = 0; i < bar.buttonRects.size() && i < emojis.size(); ++i) {
        if (!bar.buttonRects[i].contains(pos))
            continue;

        // Toggle: remove if the current user already reacted, otherwise add.
        // Reuses the same signal (and therefore the add/remove path) as the
        // reaction pill click.
        bool me = false;
        bool isBurst = false;
        const QString normalized = normalizeReactionEmoji(emojis[i]);
        for (const ReactionData &reaction : resolved.ctx.reactions) {
            if (!reaction.emojiId.isValid() && reaction.emojiName == normalized && reaction.me) {
                me = true;
                isBurst = reaction.isBurst;
                break;
            }
        }
        emit toggleReactionClicked(channelId, messageId, emojis[i], me, isBurst);
        return true;
    }

    // Inside the bar but between buttons — swallow the click.
    return true;
}

} // namespace UI
} // namespace Acheron

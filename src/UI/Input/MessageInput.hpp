#pragma once

#include <QHash>
#include <QWidget>
#include <QTextEdit>
#include <QLabel>
#include <QPixmapCache>

#include <functional>
#include <optional>

#include <QBuffer>
#include <QMovie>

#include "Core/EmojiCatalog.hpp"
#include "Core/PendingAttachment.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QGraphicsOpacityEffect;
class QNetworkAccessManager;
class QParallelAnimationGroup;
class QPropertyAnimation;
class QTimer;
class QToolButton;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel;
class EmojiAutocompletePopup;
class SlashCommandPopup;
class MentionAutocompletePopup;
struct MentionItem;

class ChatTextEdit : public QTextEdit
{
    Q_OBJECT
public:
    explicit ChatTextEdit(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
signals:
    void returnPressed();
    void escapePressed();
    void filesPasted(const QList<QUrl> &urls);
    void imagePasted(const QImage &image);
};

class MessageInput : public QWidget
{
    Q_OBJECT
public:
    explicit MessageInput(QWidget *parent = nullptr);
    void clear();
    void setPlaceholder(const QString &name);

    void setReplyTarget(Core::Snowflake messageId, const QString &authorName,
                        const QString &contentSnippet);
    void clearReplyTarget();
    [[nodiscard]] Core::Snowflake replyTargetMessageId() const { return replyMessageId; }

    void setSendBlocked(bool blocked);
    [[nodiscard]] bool isSendBlocked() const { return sendBlocked; }

    void insertText(const QString &text);
    void refreshEmojiCompleter();
    void refreshEmojiPopup();

    void queueAttachments(const QList<QUrl> &urls);
    void setMaxUploadSize(qint64 bytes);
    void setAvailableStickers(QHash<Core::Snowflake, QList<Discord::Sticker>> stickers);
    void setAvailableCommands(const QList<Discord::ApplicationCommand> &commands);
    void setAvailableMentions(const QList<MentionItem> &items);
    void setGuildOrder(const QStringList &orderedGuildIds);
    void setCurrentGuildId(const Core::Snowflake &guildId);
    void setCompact(bool compact);
    [[nodiscard]] bool isCompact() const { return compactMode; }
    void setStickerGuildInfo(Core::Snowflake guildId, const QString &guildName,
                             const QString &guildIconHash,
                             std::function<QUrl(Core::Snowflake, const QString &)> iconProvider);

    /// Mounts a slim status strip (typing indicator / slowmode countdown) at
    /// the very top of the input block, collapsed. Call setStatusStripActive()
    /// to slide it out; its height is folded into the input's height so no
    /// separate row is needed between the chat and the input bar.
    void setStatusStrip(QWidget *strip);

    /// Slides the status strip open (active) or closed (inactive). The input
    /// block's height animates in sync, so the whole bottom region glides
    /// instead of snapping.
    void setStatusStripActive(bool active);

signals:
    void stickerPicked(Core::Snowflake stickerId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

signals:
    void sendMessage(const QString &text, const QList<Core::PendingAttachment> &attachments);
    void slashCommandSend(const Discord::ApplicationCommand &command,
                          const QList<Discord::InteractionOptionValue> &options);
    void slashCommandIncomplete(const QString &reason);
    // Debounced slash-command-name prefix for server-side search.
    void slashQueryChanged(const QString &query);
    // The user is typing in the input: emit once when typing starts and then
    // periodically (every kTypingSendIntervalMs) while the draft stays
    // non-empty. The receiver decides whether to actually broadcast it (e.g.
    // the silent-typing setting) and to which channel.
    void typingTick();

private:
    ChatTextEdit *textEdit;
    QWidget *replyBar;
    QLabel *replyLabel;
    QToolButton *replyCancelButton;
    QToolButton *stickerPickerButton;
    QToolButton *gifPickerButton = nullptr;
    AttachmentPreviewPanel *attachmentPanel;
    EmojiAutocompletePopup *emojiPopup = nullptr;
    SlashCommandPopup *slashPopup = nullptr;
    MentionAutocompletePopup *mentionPopup = nullptr;
    QList<MentionItem> m_availableMentions;
    QList<Discord::ApplicationCommand> m_availableCommands;
    QTimer *slashQueryDebounce = nullptr;
    QString m_pendingSlashQuery;
    // Re-arms per keystroke while a draft is non-empty; on timeout the input
    // emits typingTick() one last time and goes quiet (single-shot), so the
    // indicator stops shortly after the user stops typing.
    QTimer *typingTimer = nullptr;
    bool typingActive = false;
    QGraphicsOpacityEffect *replyBarOpacity = nullptr;
    QPropertyAnimation *replyBarFadeAnimation = nullptr;
    QWidget *statusStrip_ = nullptr;
    QParallelAnimationGroup *stripAnimGroup_ = nullptr;

    int collapsedContentHeight() const;

    Core::Snowflake replyMessageId;
    QHash<Core::Snowflake, QList<Discord::Sticker>> availableStickers;
    Core::Snowflake stickerGuildId;
    QString stickerGuildName;
    QString stickerGuildIconHash;
    std::function<QUrl(Core::Snowflake, const QString &)> stickerGuildIconProvider;
    QNetworkAccessManager *stickerNam = nullptr;
    QNetworkAccessManager *emojiNam = nullptr;
    QNetworkAccessManager *gifNam = nullptr;
    QStringList guildOrder;
    Core::Snowflake currentGuildId;
    QHash<QString, QPointer<QMovie>> activeEmojiMovies;
    QHash<QString, QByteArray> emojiGifCache;
    QLabel *gifProgressLabel = nullptr;
    // LOW #20: Sticker preview before sending
    struct PendingSticker {
        Core::Snowflake stickerId;
        QImage previewImage;
        QString description;
    };
    std::optional<PendingSticker> pendingSticker;
    void clearPendingSticker();
    QLabel *stickerPreviewLabel = nullptr;
    bool sendBlocked = false;
    bool compactMode = false;
    quint64 replyBarAnimationGeneration = 0;
    quint64 emojiMarkerSequence = 0;

    void adjustHeight();
    void updateEmojiCompleter();
    void updateEmojiPopup(const QString &text);
    void insertEmojiCompletion(const Core::EmojiCatalogItem &item);
    void showEmojiPopup();
    void hideEmojiPopup();
    [[nodiscard]] QString currentEmojiPrefix(int *startPosition = nullptr) const;
    [[nodiscard]] QString currentEmojiPrefix(const QString &text, int *startPosition) const;
    void updateSlashPopup(const QString &text);
    void insertSlashCompletion(const Discord::ApplicationCommand &command);
    void insertSlashArgument(const QString &text);
    bool tryParseSlashCommand(const QString &text, Discord::ApplicationCommand *command,
                              QList<Discord::InteractionOptionValue> *options) const;
    void showSlashPopup();
    void hideSlashPopup();
    void updateMentionPopup(const QString &text);
    void insertMentionCompletion(const MentionItem &item);
    void showMentionPopup();
    void hideMentionPopup();
    [[nodiscard]] QString currentMentionPrefix(int *startPosition = nullptr, QChar *trigger = nullptr) const;
    [[nodiscard]] QString currentMentionPrefix(const QString &text, int *startPosition, QChar *trigger) const;
    void insertEmojiInline(const Core::EmojiCatalogItem &item);
    [[nodiscard]] int emojiInlineSize() const;
    [[nodiscard]] QString extractMessageText() const;
    void startAnimatedEmoji(const QString &emojiValue, const QByteArray &gifData);
    // Stops and releases animated-emoji movies whose image no longer appears in
    // the document (the user deleted the emoji). Looping GIFs never emit
    // finished(), so without this every typed-and-deleted emoji leaks a live
    // decoder + full-viewport repaints for the rest of the session.
    void pruneOrphanedEmojiMovies();
    void showReplyBar();
    void hideReplyBar();
    [[nodiscard]] QString makeEmojiMarker(const QString &emojiValue);
    bool replaceMarker(const QString &marker, const QImage &frame, const QString &emojiValue);
    void removeMarker(const QString &marker);
    void cleanEmojiAnimation(const QString &emojiValue);
    void pickGif();
    void downloadAndAttachGif(const QUrl &url, const QString &filename);
};

} // namespace UI
} // namespace Acheron

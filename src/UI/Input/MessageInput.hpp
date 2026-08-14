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
#include "Core/Markdown/Parser.hpp"
#include "Core/PendingAttachment.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QGraphicsOpacityEffect;
class QNetworkAccessManager;
class QPropertyAnimation;
class QSplitter;
class QTextBrowser;
class QTimer;
class QToolButton;

namespace Acheron {
namespace UI {

class AttachmentPreviewPanel;
class EmojiAutocompletePopup;

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
    void setGuildOrder(const QStringList &orderedGuildIds);
    void setCurrentGuildId(const Core::Snowflake &guildId);
    void setCompact(bool compact);
    [[nodiscard]] bool isCompact() const { return compactMode; }
    void setStickerGuildInfo(Core::Snowflake guildId, const QString &guildName,
                             const QString &guildIconHash,
                             std::function<QUrl(Core::Snowflake, const QString &)> iconProvider);

signals:
    void stickerPicked(Core::Snowflake stickerId);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

signals:
    void sendMessage(const QString &text, const QList<Core::PendingAttachment> &attachments);

private:
    ChatTextEdit *textEdit;
    QWidget *replyBar;
    QLabel *replyLabel;
    QToolButton *replyCancelButton;
    QToolButton *stickerPickerButton;
    QToolButton *gifPickerButton = nullptr;
    AttachmentPreviewPanel *attachmentPanel;
    QSplitter *previewSplitter;
    QTextBrowser *markdownPreview;
    QToolButton *markdownPreviewToggle;
    QTimer *markdownPreviewDebounceTimer = nullptr;
    Core::Markdown::Parser markdownParser;
    std::optional<QString> lastMarkdownPreviewText;
    EmojiAutocompletePopup *emojiPopup = nullptr;
    QGraphicsOpacityEffect *replyBarOpacity = nullptr;
    QPropertyAnimation *replyBarFadeAnimation = nullptr;

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
    bool markdownPreviewVisible = false;
    int markdownPreviewHeight = 96;
    quint64 replyBarAnimationGeneration = 0;
    quint64 emojiMarkerSequence = 0;

    void adjustHeight();
    void updateEmojiCompleter();
    void updateEmojiPopup();
    void insertEmojiCompletion(const Core::EmojiCatalogItem &item);
    void showEmojiPopup();
    void hideEmojiPopup();
    [[nodiscard]] QString currentEmojiPrefix(int *startPosition = nullptr) const;
    void updateMarkdownPreview();
    void renderMarkdownPreview();
    void setMarkdownPreviewVisible(bool visible);
    void insertEmojiInline(const Core::EmojiCatalogItem &item);
    [[nodiscard]] int emojiInlineSize() const;
    [[nodiscard]] QString extractMessageText() const;
    void startAnimatedEmoji(const QString &emojiValue, const QByteArray &gifData);
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

#pragma once

class QNetworkAccessManager;

#include <QAbstractListModel>
#include <QCache>
#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QList>
#include <QModelIndex>
#include <QMovie>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QString>
#include <QTextDocument>
#include <QUrl>
#include <QVariant>
#include <QVector>
#include "Core/Session.hpp"
#include "Core/MessageManager.hpp"
#include "UI/AvatarRequestTracker.hpp"

namespace Acheron {

namespace Core {
class ImageManager;
class GifAnimation;
} // namespace Core

using Core::Snowflake;

struct AttachmentData
{
    Snowflake id;
    QUrl proxyUrl;
    QUrl originalUrl;
    QSize displaySize;
    QPixmap pixmap;
    bool isLoading = false;
    bool isImage = false;
    bool isGif = false;
    bool isVideo = false;
    bool isVoice = false;
    QString filename;
    qint64 fileSizeBytes = 0;
    bool isSpoiler = false;
    qint64 uploadSent = -1;
    qint64 uploadTotal = -1;
    // Voice message fields
    QByteArray waveform;      // base64-decoded waveform samples (0-255)
    double durationSecs = 0;  // duration in seconds
};

struct EmbedFieldData
{
    QString name;
    QString value;
    QString nameParsed;
    QString valueParsed;
    bool isInline = false;
};

struct EmbedImageData
{
    QUrl url;
    QPixmap pixmap;
    QSize displaySize;
};

enum class EmbedType {
    AgeVerificationSystemNotification,
    ApplicationNews,
    Article,
    AutoModerationMessage,
    AutoModerationNotification,
    Gift,
    Gifv,
    Image,
    Link,
    PollResult,
    PostPreview,
    Rich,
    SafetyPolicyNotice,
    SafetySystemNotification,
    Video,
};

struct ReplyData
{
    enum class State {
        None, // not a reply
        Present, // referenced message is available
        Deleted, // referenced message was deleted
        Unknown, // backend didn't fetch referenced message
    };

    State state = State::None;
    Core::Snowflake referencedMessageId;
    QString authorName;
    QColor authorColor;
    Core::Snowflake authorId;
    QString contentSnippet;
};

struct StickerData
{
    Core::Snowflake id;
    QString name;
    Discord::StickerFormatType formatType;
    QUrl cdnUrl;
    QPixmap pixmap;
    bool isLoading = false;
    bool isAnimated = false;
    QPixmap currentFrame; // Updated by QMovie for APNG/GIF stickers
};

struct ReactionData
{
    QString emojiName;
    Core::Snowflake emojiId;
    bool emojiAnimated = false;
    int count = 0;
    bool me = false;
    bool isBurst = false;
    QPixmap emojiPixmap;
    bool isLoading = false;
    QColor burstTintColor;
};

struct PollAnswerData
{
    int id = 0;
    QString text;
    QString emojiName; // unicode glyph for built-in emoji, else the custom emoji name
    Core::Snowflake emojiId;
    int count = 0;
    double percent = 0.0; // share of total votes, 0..100
    bool me = false;      // current user voted for this answer
};

struct PollData
{
    QString question;
    QList<PollAnswerData> answers;
    bool allowMultiselect = false;
    bool isFinalized = false;
    bool isExpired = false;
    QList<int> myAnswers; // answer ids the current user voted for
    int totalVotes = 0;
};

struct EmbedData
{
    EmbedType type = EmbedType::Rich; // should this be default idk
    QString title;
    QString description;
    QString titleParsed;
    QString descriptionParsed;
    QString url;
    QDateTime timestamp;
    QColor color;

    QString authorName;
    QString authorUrl;
    QUrl authorIconUrl;
    QPixmap authorIcon;

    QString footerText;
    QUrl footerIconUrl;
    QPixmap footerIcon;

    QUrl thumbnailUrl;
    QPixmap thumbnail;
    QSize thumbnailSize;

    QList<EmbedImageData> images;

    QUrl videoThumbnailUrl;
    QPixmap videoThumbnail;
    QSize videoThumbnailSize;

    QUrl videoUrl; // actual media URL (embed.video) used for in-app playback

    QString providerName;
    QString providerUrl;

    QList<EmbedFieldData> fields;
};

namespace UI {

struct DocCacheKey
{
    Snowflake messageId;
    // 0 = body, then embed/field sub-documents encoded as:
    // (embedIndex+1)*1000 + offset (0=title, 1=desc, 100+fieldIndex*2=fieldName, 100+fieldIndex*2+1=fieldValue)
    int subId = 0;

    bool operator==(const DocCacheKey &o) const
    {
        return messageId == o.messageId && subId == o.subId;
    }
};

inline size_t qHash(const DocCacheKey &k, size_t seed = 0)
{
    return qHashMulti(seed, quint64(k.messageId), k.subId);
}

inline DocCacheKey bodyDocKey(Snowflake msgId)
{
    return { msgId, 0 };
}
inline DocCacheKey embedTitleDocKey(Snowflake msgId, int embedIdx)
{
    return { msgId, (embedIdx + 1) * 1000 };
}
inline DocCacheKey embedDescDocKey(Snowflake msgId, int embedIdx)
{
    return { msgId, (embedIdx + 1) * 1000 + 1 };
}
inline DocCacheKey embedFieldNameDocKey(Snowflake msgId, int embedIdx, int fieldIdx)
{
    return { msgId, (embedIdx + 1) * 1000 + 100 + fieldIdx * 2 };
}
inline DocCacheKey embedFieldValueDocKey(Snowflake msgId, int embedIdx, int fieldIdx)
{
    return { msgId, (embedIdx + 1) * 1000 + 100 + fieldIdx * 2 + 1 };
}
inline DocCacheKey pollQuestionDocKey(Snowflake msgId)
{
    return { msgId, 9000 };
}

class ChatModel : public QAbstractListModel
{
    Q_OBJECT
public:
    ChatModel(Core::ImageManager *imageManager, QObject *parent = nullptr);

    enum Roles {
        ContentRole = Qt::UserRole + 1,
        UsernameRole,
        UserIdRole,
        AvatarRole,
        TimestampRole,
        EditedTimestampRole,
        CachedSizeRole,
        ShowHeaderRole,
        DateSeparatorRole,
        HtmlRole,
        AttachmentsRole,
        EmbedsRole,
        IsPendingRole,
        IsErroredRole,
        UsernameColorRole,
        MessageIdRole,
        ReplyDataRole,
        ReactionsRole,
        StickersRole,
        IsSystemMessageRole,
        PollRole,
    };

    using AvatarUrlResolver = std::function<QUrl(const Discord::User &)>;
    void setAvatarUrlResolver(AvatarUrlResolver resolver);

    using DisplayNameResolver = std::function<QString(Core::Snowflake userId, Core::Snowflake guildId)>;
    void setDisplayNameResolver(DisplayNameResolver resolver);

    using RoleColorResolver = std::function<QColor(Core::Snowflake userId, Core::Snowflake guildId)>;
    void setRoleColorResolver(RoleColorResolver resolver);

    using ChannelNameResolver = std::function<QString(Core::Snowflake channelId)>;
    void setChannelNameResolver(ChannelNameResolver resolver);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;

    /// Writes the cached layout size for a row directly, without emitting
    /// dataChanged. Safe for the delegate to call during the view's layout
    /// pass, unlike setData which can re-enter layout via dataChanged.
    void setCachedSize(const QModelIndex &index, const QSize &size) const;

    /// Row of a loaded message, or -1 when it isn't in the current channel view.
    [[nodiscard]] int rowForMessage(Core::Snowflake messageId) const;

    struct MessageSearchHit
    {
        Core::Snowflake messageId;
        QString authorName;
        Core::Snowflake authorId;
        QString authorAvatarHash;
        QString content;      // raw (non-HTML) content
        qint64 timestampSecs = 0;
        int row = -1;         // row in this model, -1 when the message isn't loaded
    };

    /// Case-insensitive substring search over the messages currently loaded for
    /// the active channel. Results are ordered oldest → newest.
    [[nodiscard]] QList<MessageSearchHit> searchLoadedMessages(const QString &query, int limit = 200) const;

    [[nodiscard]] Core::Snowflake getOldestMessageId() const;
    [[nodiscard]] Core::Snowflake getActiveChannelId() const;
    [[nodiscard]] Core::Snowflake getActiveGuildId() const;
    [[nodiscard]] bool isSpoilerRevealed(Core::Snowflake attachmentId) const;

    QTextDocument *getCachedDocument(const DocCacheKey &key) const;
    void cacheDocument(const DocCacheKey &key, QTextDocument *doc) const;
    void invalidateDocCache();
    void invalidateDocCacheForMessage(Core::Snowflake messageId);
    void invalidateLayout();

    /// Returns the GifAnimation for the given attachment URL, creating it if necessary.
    /// The delegate calls this during paint() to get the current GIF frame.
    Core::GifAnimation *ensureGifAnimation(const QUrl &url, int row) const;

    /// True if the ImageManager fetch for this url+size definitively failed;
    /// the delegate paints an error state instead of the gray placeholder.
    [[nodiscard]] bool hasImageFailed(const QUrl &url, const QSize &size) const;

signals:
    /// A poll answer was clicked in the message view. `answerIds` holds the
    /// answer id(s) to vote for. The view should route this to Client::votePoll;
    /// the resulting MESSAGE_UPDATE refreshes the row's poll results.
    void pollVoteRequested(Core::Snowflake channelId, Core::Snowflake messageId,
                           const QList<int> &answerIds);

public slots:
    void setActiveChannel(Snowflake channelId, Snowflake guildId = Snowflake::Invalid);
    void handleIncomingMessages(const Core::MessageRequestResult &result);
    void handleMessageDeleted(Snowflake channelId, Snowflake messageId);
    void handleMessageErrored(const QString &nonce);
    void handleUploadProgress(const QString &nonce, int fileIndex, qint64 sent, qint64 total);
    /// Register a nonce as a local pending send so the matching outbound
    /// preview is rendered in the pending state. Called by MessageManager's
    /// messageSendPending signal or by the MainWindow send path.
    void addPendingNonce(const QString &nonce);
    void handleSendPending(const QString &nonce) { addPendingNonce(nonce); }
    void refreshUsersInView(const QList<Snowflake> &userIds);
    void revealSpoiler(Snowflake attachmentId);

    void triggerResize(int row)
    {
        if (row < 0 || row >= rowCount())
            return;
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { CachedSizeRole });
    }

    QMovie *stickerMovie(Core::Snowflake stickerId, const QUrl &cdnUrl,
                         Discord::StickerFormatType formatType);
    void cleanupStickerMovies(const QList<Core::Snowflake> &visibleStickerIds);
    void cleanupGifAnimations(const QList<QUrl> &visibleUrls);

private slots:
    void onStickerFrameChanged();

private:
    QString resolveAuthorName(const Discord::User &author) const;
    QColor resolveAuthorColor(const Discord::User &author) const;
    QPixmap localPixmap(const QUrl &url, const QSize &displaySize) const;
    QPixmap previewPixmap(Snowflake attachmentId, const QImage &image, const QSize &displaySize) const;
    void prunePreviewCaches(const Discord::Message &msg);

    QList<AttachmentData> buildAttachmentData(const Discord::Message &msg) const;
    QList<ReactionData> buildReactionData(const Discord::Message &msg) const;
    PollData buildPollData(const Discord::Message &msg) const;
    void warmCachesForMessage(const Discord::Message &msg);
    void ensureStickerMoviesForRow(int row, const QList<StickerData> &stickers);

    void shiftMessageRows(int startRow, int delta);
    void insertMessageRow(Snowflake messageId, int row);
    void removeMessageRow(Snowflake messageId);
    void replaceMessageRow(Snowflake oldId, Snowflake newId, int row);

    // Emoji/sticker/media reverse index: URL -> messages referencing it, so
    // imageFetched only has to touch the rows that actually reference the URL.
    struct EmojiUrlRefs
    {
        QSet<Snowflake> content;  // referenced in parsed content or embed text
        QSet<Snowflake> reactions;
        QSet<Snowflake> stickers;
        QSet<Snowflake> images;   // attachments, embed author/footer/thumbnail/image/video
    };
    void indexMessageEmojiUrls(const Discord::Message &msg);
    void indexEmbedEmojiUrls(Snowflake messageId, const QList<EmbedData> &embeds) const;
    void notifyImageSettled(const QUrl &url);
    void unindexMessageEmojiUrls(Snowflake messageId);
    void ensureMessageRowIndex() const;
    void trimOldestMessagesIfNeeded();

    /// Cap on messages kept loaded per channel; oldest are trimmed from the top.
    static constexpr int MaxLoadedMessages = 500;

    Core::ImageManager *imageManager;
    QVector<Discord::Message> messages;
    mutable QHash<Snowflake, QSize> sizeCache;
    mutable QHash<Snowflake, QList<EmbedData>> embedCache;
    mutable QHash<Snowflake, QList<AttachmentData>> attachmentCache;
    mutable QHash<Snowflake, QList<ReactionData>> reactionCache;
    mutable QCache<DocCacheKey, QTextDocument> docCache{ 500 };
    mutable QHash<Snowflake, QSet<int>> docCacheSubIds;

    Snowflake currentChannelId = Snowflake::Invalid;
    Snowflake currentGuildId = Snowflake::Invalid;

    AvatarUrlResolver avatarUrlResolver;
    DisplayNameResolver displayNameResolver;
    RoleColorResolver roleColorResolver;
    ChannelNameResolver channelNameResolver;

    mutable AvatarRequestTracker<QPersistentModelIndex> avatarTracker;
    QSet<QString> pendingNonces;
    QSet<QString> erroredNonces;
    QHash<QString, QVector<QPair<qint64, qint64>>> uploadProgress; // by nonce
    mutable QHash<QString, QPixmap> localPixmapCache; // file previews, keyed by path + size
    QHash<QString, EmojiUrlRefs> emojiUrlIndex; // emoji/sticker URL -> referencing message ids
    QHash<Snowflake, QSet<QString>> emojiUrlsByMessage; // message id -> indexed URLs (for eviction)
    mutable QHash<Snowflake, int> messageRowById; // message id -> current row (lazily rebuilt)
    mutable bool messageRowIndexDirty = true;
    QNetworkAccessManager *stickerNetworkManager = nullptr; // shared NAM for animated stickers
    mutable QHash<Snowflake, QPixmap> previewPixmapCache; // pasted bitmap previews by attachment id
    mutable QSet<Snowflake> revealedSpoilers;
    mutable bool suppressImageFetch = false;
    mutable QHash<Snowflake, QMovie *> stickerMovies; // animated sticker movies by sticker id
    mutable QHash<Snowflake, QList<QPersistentModelIndex>> stickerMovieRows; // track rows with animated stickers
    mutable QHash<QUrl, Core::GifAnimation *> gifAttachmentAnimations; // GIF animations by attachment proxy URL
    mutable QHash<QUrl, QList<QPersistentModelIndex>> gifAnimationRows; // track rows with active GIF animations
    mutable QSet<QUrl> failedGifUrls; // GIF URLs that failed to load/decode; don't re-create every paint

    mutable QSet<Snowflake> newMessageIds; // IDs of recently inserted messages for highlight animation

    friend class ChatDelegate;
};
} // namespace UI
} // namespace Acheron

Q_DECLARE_METATYPE(Acheron::StickerData)
Q_DECLARE_METATYPE(QList<Acheron::StickerData>)
Q_DECLARE_METATYPE(Acheron::ReplyData)
Q_DECLARE_METATYPE(Acheron::AttachmentData)
Q_DECLARE_METATYPE(QList<Acheron::AttachmentData>)
Q_DECLARE_METATYPE(Acheron::EmbedFieldData)
Q_DECLARE_METATYPE(Acheron::EmbedImageData)
Q_DECLARE_METATYPE(Acheron::EmbedData)
Q_DECLARE_METATYPE(QList<Acheron::EmbedData>)
Q_DECLARE_METATYPE(Acheron::ReactionData)
Q_DECLARE_METATYPE(QList<Acheron::ReactionData>)
Q_DECLARE_METATYPE(Acheron::PollAnswerData)
Q_DECLARE_METATYPE(Acheron::PollData)
Q_DECLARE_METATYPE(QList<Acheron::PollAnswerData>)

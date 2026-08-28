#include "ChatModel.hpp"

#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QRegularExpression>

#include "Core/Markdown/Parser.hpp"
#include "Core/MessageManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/AnimationUtils.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Enums.hpp"

namespace Acheron {
namespace UI {

static bool isSystemMessageType(Discord::MessageType type)
{
    switch (type) {
    case Discord::MessageType::DEFAULT:
    case Discord::MessageType::REPLY:
    case Discord::MessageType::CHAT_INPUT_COMMAND:
    case Discord::MessageType::CONTEXT_MENU_COMMAND:
        return false;
    default:
        return true;
    }
}

static EmbedType embedTypeFromString(const QString &typeStr)
{
    if (typeStr.isEmpty() || typeStr == "rich")
        return EmbedType::Rich;
    else if (typeStr == "age_verification_system_notification")
        return EmbedType::AgeVerificationSystemNotification;
    else if (typeStr == "application_news")
        return EmbedType::ApplicationNews;
    else if (typeStr == "article")
        return EmbedType::Article;
    else if (typeStr == "auto_moderation_message")
        return EmbedType::AutoModerationMessage;
    else if (typeStr == "auto_moderation_notification")
        return EmbedType::AutoModerationNotification;
    else if (typeStr == "gift")
        return EmbedType::Gift;
    else if (typeStr == "gifv")
        return EmbedType::Gifv;
    else if (typeStr == "image")
        return EmbedType::Image;
    else if (typeStr == "link")
        return EmbedType::Link;
    else if (typeStr == "poll_result")
        return EmbedType::PollResult;
    else if (typeStr == "post_preview")
        return EmbedType::PostPreview;
    else if (typeStr == "rich")
        return EmbedType::Rich;
    else if (typeStr == "safety_policy_notice")
        return EmbedType::SafetyPolicyNotice;
    else if (typeStr == "safety_system_notification")
        return EmbedType::SafetySystemNotification;
    else if (typeStr == "video")
        return EmbedType::Video;
    return EmbedType::Rich;
}

static bool embedMediaLooksLikeImage(const Discord::EmbedMedia &media)
{
    if (media.contentType.hasValue() && media.contentType->startsWith("image/"))
        return true;

    if (media.width.hasValue() && media.height.hasValue() &&
        *media.width > 0 && *media.height > 0)
        return true;

    const QString url = media.proxyUrl.hasValue() ? *media.proxyUrl
                          : (media.url.hasValue() ? *media.url : QString());
    if (!url.isEmpty()) {
        static const QStringList imageExtensions = {
            QStringLiteral(".png"),
            QStringLiteral(".jpg"),
            QStringLiteral(".jpeg"),
            QStringLiteral(".gif"),
            QStringLiteral(".webp"),
            QStringLiteral(".bmp"),
        };
        const QString lower = url.toLower();
        for (const QString &ext : imageExtensions) {
            if (lower.endsWith(ext))
                return true;
        }
    }

    return false;
}

ChatModel::ChatModel(Core::ImageManager *imageManager, QObject *parent)
    : QAbstractListModel(parent), imageManager(imageManager)
{
    connect(imageManager, &Core::ImageManager::imageFetched, this,
            [this](const QUrl &url, const QSize &size, const QPixmap &pixmap) {
                // avatar pending requests
                avatarTracker.notify(url, [this](const QModelIndex &index) {
                    if (index.isValid())
                        emit dataChanged(index, index, { Qt::DecorationRole });
                });

                // custom emoji in message content, embed text, reactions, stickers
                if (url.host() == u"cdn.discordapp.com" && url.path().startsWith(u"/emojis/")) {
                    const QString urlStr = url.toString();
                    auto it = emojiUrlIndex.constFind(urlStr);
                    if (it == emojiUrlIndex.constEnd())
                        return;

                    ensureMessageRowIndex();
                    const EmojiUrlRefs &refs = it.value();

                    for (const Snowflake id : refs.content) {
                        const int row = messageRowById.value(id, -1);
                        if (row < 0)
                            continue;
                        invalidateDocCacheForMessage(id);
                        // The doc grew after the emoji image loaded; drop the
                        // cached row height so the row re-measures.
                        sizeCache.remove(id);
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, { HtmlRole, EmbedsRole, CachedSizeRole });
                    }

                    for (const Snowflake id : refs.reactions) {
                        const int row = messageRowById.value(id, -1);
                        if (row < 0)
                            continue;
                        reactionCache.remove(id);
                        pollCache.remove(id);
                        sizeCache.remove(id);
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, { ReactionsRole, CachedSizeRole });
                    }

                    for (const Snowflake id : refs.stickers) {
                        const int row = messageRowById.value(id, -1);
                        if (row < 0)
                            continue;
                        sizeCache.remove(id);
                        QModelIndex idx = index(row, 0);
                        emit dataChanged(idx, idx, { StickersRole, CachedSizeRole });
                    }
                    return;
                }

                // attachment and embed images
                notifyImageSettled(url);
            });

    connect(imageManager, &Core::ImageManager::imageFailed, this,
            [this](const QUrl &url, const QSize &) {
                // A failed fetch never emits imageFetched, so rows holding the
                // placeholder would never repaint; notify them so the delegate
                // can paint an error state instead of an eternal gray box.
                notifyImageSettled(url);
            });
}

void ChatModel::notifyImageSettled(const QUrl &url)
{
    const QString urlStr = url.toString();
    auto it = emojiUrlIndex.constFind(urlStr);
    if (it == emojiUrlIndex.constEnd())
        return;

    ensureMessageRowIndex();
    const EmojiUrlRefs &refs = it.value();

    for (const Snowflake id : refs.content) {
        const int row = messageRowById.value(id, -1);
        if (row < 0)
            continue;
        invalidateDocCacheForMessage(id);
        // The doc grew after the emoji image loaded; drop the cached row
        // height so the row re-measures.
        sizeCache.remove(id);
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { HtmlRole, EmbedsRole, CachedSizeRole });
    }

    for (const Snowflake id : refs.reactions) {
        const int row = messageRowById.value(id, -1);
        if (row < 0)
            continue;
        reactionCache.remove(id);
        pollCache.remove(id);
        sizeCache.remove(id);
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { ReactionsRole, CachedSizeRole });
    }

    for (const Snowflake id : refs.stickers) {
        const int row = messageRowById.value(id, -1);
        if (row < 0)
            continue;
        sizeCache.remove(id);
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { StickersRole, CachedSizeRole });
    }

    for (const Snowflake id : refs.images) {
        const int row = messageRowById.value(id, -1);
        if (row < 0)
            continue;
        attachmentCache.remove(id);
        embedCache.remove(id);
        sizeCache.remove(id);
        invalidateDocCacheForMessage(id);
        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, { AttachmentsRole, EmbedsRole, CachedSizeRole });
    }
}

void ChatModel::setAvatarUrlResolver(AvatarUrlResolver resolver)
{
    avatarUrlResolver = std::move(resolver);
}

void ChatModel::setDisplayNameResolver(DisplayNameResolver resolver)
{
    displayNameResolver = std::move(resolver);
}

void ChatModel::setRoleColorResolver(RoleColorResolver resolver)
{
    roleColorResolver = std::move(resolver);
}

void ChatModel::setChannelNameResolver(ChannelNameResolver resolver)
{
    channelNameResolver = std::move(resolver);
}

QString ChatModel::resolveAuthorName(const Discord::User &author) const
{
    if (displayNameResolver) {
        QString name = displayNameResolver(author.id.get(), currentGuildId);
        if (!name.isEmpty())
            return name;
    }
    return author.getDisplayName();
}

QColor ChatModel::resolveAuthorColor(const Discord::User &author) const
{
    if (!roleColorResolver || currentGuildId == Snowflake::Invalid)
        return {};
    return roleColorResolver(author.id.get(), currentGuildId);
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return messages.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const auto &msg = messages[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        [[fallthrough]];
    case ContentRole:
        return msg.content;
    case UsernameRole:
        return resolveAuthorName(msg.author.get());
    case AvatarRole: {
        const QSize desiredSize(32, 32);

        if (!avatarUrlResolver)
            return imageManager->placeholder(desiredSize);

        QUrl url = avatarUrlResolver(msg.author.get());
        return avatarTracker.fetch(imageManager, url, desiredSize, index, Core::PinGroup::ChatView);
    }
    case TimestampRole:
        return msg.timestamp;
    case EditedTimestampRole:
        return msg.editedTimestamp.hasValue() ? QVariant(*msg.editedTimestamp) : QVariant();
    case UserIdRole:
        return msg.author->id;
    case CachedSizeRole: {
        if (sizeCache.contains(msg.id))
            return sizeCache.value(msg.id);
        return {};
    }
    case ShowHeaderRole: {
        if (isSystemMessageType(msg.type))
            return false;

        // replies always show a header
        if (msg.type == Discord::MessageType::REPLY)
            return true;

        if (index.row() == 0)
            return true;

        const auto &prevMsg = messages[index.row() - 1];

        if (prevMsg.author->id != msg.author->id)
            return true;

        if (prevMsg.timestamp->toLocalTime().date() != msg.timestamp->toLocalTime().date())
            return true;

        return false;
    }
    case DateSeparatorRole: {
        if (index.row() == 0)
            return true;

        const auto &prevMsg = messages[index.row() - 1];

        if (prevMsg.timestamp->toLocalTime().date() != msg.timestamp->toLocalTime().date())
            return true;

        return false;
    }
    case HtmlRole: {
        if (msg.type.get() == Discord::MessageType::THREAD_CREATED) {
            QString threadName = msg.content.hasValue() ? msg.content.get().toHtmlEscaped() : QString();
            Core::Snowflake threadId = Core::Snowflake::Invalid;
            if (msg.messageReference.hasValue() && msg.messageReference->channelId.hasValue())
                threadId = msg.messageReference->channelId.get();
            else if (msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::HAS_THREAD))
                threadId = msg.id.get();

            QString authorName = resolveAuthorName(msg.author.get()).toHtmlEscaped();
            QColor authorColor = resolveAuthorColor(msg.author.get());
            QString authorHtml = authorColor.isValid()
                                         ? QStringLiteral("<span style=\"color:%1;font-weight:600\">%2</span>")
                                                   .arg(authorColor.name(), authorName)
                                         : QStringLiteral("<b>%1</b>").arg(authorName);

            QString label = threadName.isEmpty() ? tr("a thread") : threadName;
            QString threadHtml = threadId.isValid()
                                         ? QStringLiteral("<a href=\"acheron://channel/%1\">%2</a>")
                                                   .arg(QString::number(static_cast<quint64>(threadId)), label)
                                         : QStringLiteral("<b>%1</b>").arg(label);

            return authorHtml + tr(" started a thread: ") + threadHtml;
        }

        if (msg.type.get() == Discord::MessageType::THREAD_STARTER_MESSAGE) {
            if (msg.referencedMessage) {
                if (!msg.referencedMessage->parsedContentCached.isEmpty())
                    return msg.referencedMessage->parsedContentCached;
                if (msg.referencedMessage->content.hasValue())
                    return msg.referencedMessage->content.get().toHtmlEscaped();
                return QString();
            }
            return tr("Sorry, we couldn't load the first message in this thread.");
        }

        // Forwarded messages (message_reference.type == FORWARD) get a banner.
        // Computed first so image-embed-only forwards still show the banner.
        QString forwardBanner;
        if (msg.messageReference.hasValue() && msg.messageReference->type.get() == 1) {
            QString channelName;
            if (channelNameResolver && msg.messageReference->channelId.hasValue())
                channelName = channelNameResolver(msg.messageReference->channelId.get());
            const QString label = tr("Forwarded from %1")
                                          .arg(channelName.isEmpty() ? QStringLiteral("a channel")
                                                                     : QStringLiteral("#") + channelName);
            forwardBanner = QStringLiteral(
                                    "<div style=\"font-size:11px; color:palette(placeholder-text); "
                                    "font-weight:600; margin-bottom:2px;\">%1</div>")
                                    .arg(label.toHtmlEscaped());
        }

        // for image embeds, suppress text if content is just the embed url
        if (msg.embeds.hasValue() && msg.embeds->size() == 1) {
            const auto &embed = msg.embeds->first();
            QString embedType = embed.type.hasValue() ? *embed.type : QString();
            if (embedType == "image") {
                QString embedUrl = embed.url.hasValue() ? *embed.url : QString();
                if (!embedUrl.isEmpty() && msg.content == embedUrl)
                    return forwardBanner;
            }
        }

        QString html = forwardBanner + msg.parsedContentCached;

        if (msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::HAS_THREAD)) {
            QString sep = html.isEmpty() ? QString() : QStringLiteral("<br>");
            html += sep +
                    QStringLiteral("<a href=\"acheron://channel/%1\">"
                                   "<img src=\"acheron-icon:view-thread\" width=\"14\" height=\"14\""
                                   " style=\"vertical-align: middle\">"
                                   " %2</a>")
                            .arg(QString::number(static_cast<quint64>(msg.id.get())),
                                 tr("View Thread"));
        }

        return html;
    }
    case AttachmentsRole: {
        if (!msg.attachments.hasValue() || msg.attachments->isEmpty())
            return QVariant();

        auto it = attachmentCache.find(msg.id);
        if (it != attachmentCache.end())
            return QVariant::fromValue(*it);

        QList<AttachmentData> data = buildAttachmentData(msg);
        // Do not cache during the sizeHint pass: it runs with
        // suppressImageFetch set, so only getIfCached() placeholders would be
        // stored. Caching those freezes the gray box and stops the paint pass
        // from ever issuing the real network fetch.
        if (!suppressImageFetch)
            attachmentCache.insert(msg.id, data);
        return QVariant::fromValue(data);
    }
    case EmbedsRole: {
        if (!msg.embeds.hasValue() || msg.embeds->isEmpty())
            return QVariant();

        if (embedCache.contains(msg.id))
            return QVariant::fromValue(embedCache.value(msg.id));

        QList<EmbedData> result;
        // for handling the url-based embed image merging
        QMap<QString, int> urlToEmbedIndex;

        for (const auto &embed : *msg.embeds) {
            QString embedUrl = embed.url.hasValue() ? *embed.url : QString();

            bool hasImage = embed.image.hasValue() && embed.image->proxyUrl.hasValue() &&
                            embedMediaLooksLikeImage(*embed.image);

            bool shouldMerge = false;
            int parentIndex = -1;

            if (!embedUrl.isEmpty() && hasImage && urlToEmbedIndex.contains(embedUrl)) {
                parentIndex = urlToEmbedIndex[embedUrl];
                // excess ignored
                if (result[parentIndex].images.size() < 4)
                    shouldMerge = true;
            }

            if (shouldMerge) {
                EmbedImageData imageData;
                imageData.url = QUrl(*embed.image->proxyUrl);
                QSize origSize;
                if (embed.image->width.hasValue() && embed.image->height.hasValue())
                    origSize = QSize(*embed.image->width, *embed.image->height);
                imageData.displaySize = Core::ImageManager::calculateDisplaySize(origSize);
                imageData.pixmap =
                        suppressImageFetch
                                ? imageManager->getIfCached(imageData.url, imageData.displaySize)
                                : imageManager->get(imageData.url, imageData.displaySize);

                result[parentIndex].images.append(imageData);
            } else if (!shouldMerge && hasImage && !embedUrl.isEmpty() &&
                       urlToEmbedIndex.contains(embedUrl)) {
                continue;
            } else {
                EmbedData data;

                bool hasAnything = embed.title.hasValue() || embed.description.hasValue() ||
                                   embed.timestamp.hasValue() || embed.color.hasValue() ||
                                   embed.author.hasValue() || embed.footer.hasValue() ||
                                   embed.provider.hasValue() || embed.video.hasValue() || hasImage;

                data.type = embedTypeFromString(embed.type.hasValue() ? *embed.type : QString());
                data.title = embed.title.hasValue() ? *embed.title : QString();
                data.description = embed.description.hasValue() ? *embed.description : QString();
                data.url = embedUrl;
                data.timestamp = embed.timestamp.hasValue() ? *embed.timestamp : QDateTime();
                data.color = embed.color.hasValue()
                                     ? QColor::fromRgb(*embed.color)
                                     : Core::Theme::Manager::instance().color(Core::Theme::Token::EmbedDefault);

                static Core::Markdown::Parser parser;
                Core::Markdown::ParseState titleState;
                titleState.isInline = true;
                titleState.excludedRules.insert("link");
                if (!data.title.isEmpty()) {
                    auto ast = parser.parse(data.title, titleState);
                    data.titleParsed = parser.toHtml(ast);
                }

                Core::Markdown::ParseState descriptionState;
                descriptionState.isInline = true;
                if (!data.description.isEmpty()) {
                    auto ast = parser.parse(data.description, descriptionState);
                    data.descriptionParsed = parser.toHtml(ast);
                }

                if (embed.author.hasValue()) {
                    data.authorName =
                            embed.author->name.hasValue() ? *embed.author->name : QString();
                    data.authorUrl = embed.author->url.hasValue()
                                             ? *embed.author->url
                                             : QString();
                    if (embed.author->proxyIconUrl.hasValue()) {
                        data.authorIconUrl = QUrl(*embed.author->proxyIconUrl);
                        data.authorIcon =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.authorIconUrl,
                                                                    QSize(24, 24))
                                        : imageManager->get(data.authorIconUrl, QSize(24, 24));
                    }
                }

                if (embed.footer.hasValue()) {
                    data.footerText =
                            embed.footer->text.hasValue() ? *embed.footer->text : QString();
                    if (embed.footer->proxyIconUrl.hasValue()) {
                        data.footerIconUrl = QUrl(*embed.footer->proxyIconUrl);
                        data.footerIcon =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.footerIconUrl,
                                                                    QSize(20, 20))
                                        : imageManager->get(data.footerIconUrl, QSize(20, 20));
                    }
                }

                if (embed.provider.hasValue()) {
                    data.providerName =
                            embed.provider->name.hasValue() ? *embed.provider->name : QString();
                    data.providerUrl =
                            embed.provider->url.hasValue() ? *embed.provider->url : QString();
                }

                // observed png thumbnail with width/height but no content type.
                // gifv/image embeds (e.g. klipy) may omit dimensions; still
                // take the thumbnail so the GIF/preview can render.
                const bool isFullBleedEmbed =
                        data.type == EmbedType::Gifv || data.type == EmbedType::Image;
                const bool isVideoEmbed = data.type == EmbedType::Video;
                if (embed.thumbnail.hasValue() && embed.thumbnail->proxyUrl.hasValue() &&
                    (embed.thumbnail->width > 0 || isFullBleedEmbed)) {
                    hasAnything = true;
                    data.thumbnailUrl = QUrl(*embed.thumbnail->proxyUrl);
                    QSize origSize;
                    if (embed.thumbnail->width.hasValue() && embed.thumbnail->height.hasValue())
                        origSize = QSize(*embed.thumbnail->width, *embed.thumbnail->height);

                    // Video embeds render the full-bleed preview from
                    // videoThumbnail below, so skip the small side thumbnail
                    // (which would otherwise reserve a thumbnail gutter).
                    if (data.type == EmbedType::Gifv || data.type == EmbedType::Image) {
                        data.thumbnailSize = Core::ImageManager::calculateDisplaySize(origSize);
                        data.thumbnail =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.thumbnailUrl,
                                                                    data.thumbnailSize)
                                        : imageManager->get(data.thumbnailUrl, data.thumbnailSize);
                    } else if (!isVideoEmbed) {
                        data.thumbnailSize = origSize.isValid()
                                                     ? origSize.scaled(80, 80, Qt::KeepAspectRatio)
                                                     : QSize(80, 80);
                        data.thumbnail =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.thumbnailUrl,
                                                                    data.thumbnailSize)
                                        : imageManager->get(data.thumbnailUrl, data.thumbnailSize);
                    }
                }

                if (hasImage) {
                    EmbedImageData imageData;
                    imageData.url = QUrl(*embed.image->proxyUrl);
                    QSize origSize;
                    if (embed.image->width.hasValue() && embed.image->height.hasValue())
                        origSize = QSize(*embed.image->width, *embed.image->height);
                    imageData.displaySize = Core::ImageManager::calculateDisplaySize(origSize);
                    imageData.pixmap =
                            suppressImageFetch
                                    ? imageManager->getIfCached(imageData.url,
                                                                imageData.displaySize)
                                    : imageManager->get(imageData.url, imageData.displaySize);
                    data.images.append(imageData);
                }

                if (embed.video.hasValue()) {
                    // Remember the actual media URL so clicking the video's
                    // play button can play it in-app (QMediaPlayer needs a
                    // direct media stream, not the page URL).
                    if (embed.video->url.hasValue() && !embed.video->url->isEmpty())
                        data.videoUrl = QUrl(*embed.video->url);
                    else if (embed.video->proxyUrl.hasValue() && !embed.video->proxyUrl->isEmpty())
                        data.videoUrl = QUrl(*embed.video->proxyUrl);

                    if (embed.thumbnail.hasValue() && embed.thumbnail->proxyUrl.hasValue() &&
                        embed.thumbnail->proxyUrl->startsWith("https://")) {
                        hasAnything = true;
                        data.videoThumbnailUrl = QUrl(*embed.thumbnail->proxyUrl);
                        QSize origSize;
                        if (embed.thumbnail->width.hasValue() && embed.thumbnail->height.hasValue())
                            origSize = QSize(*embed.thumbnail->width, *embed.thumbnail->height);
                        data.videoThumbnailSize =
                                Core::ImageManager::calculateDisplaySize(origSize);
                        data.videoThumbnail =
                                suppressImageFetch
                                        ? imageManager->getIfCached(data.videoThumbnailUrl,
                                                                    data.videoThumbnailSize)
                                        : imageManager->get(data.videoThumbnailUrl,
                                                            data.videoThumbnailSize);
                    }
                }

                if (embed.fields.hasValue()) {
                    if (!embed.fields->empty())
                        hasAnything = true;
                    for (const auto &field : *embed.fields) {
                        EmbedFieldData fieldData;
                        fieldData.name = field.name.hasValue() ? *field.name : QString();
                        fieldData.value = field.value.hasValue() ? *field.value : QString();
                        fieldData.isInline = field.isInline.hasValue() ? *field.isInline : false;

                        Core::Markdown::ParseState nameState;
                        nameState.isInline = true;
                        nameState.excludedRules.insert("link");
                        if (!fieldData.name.isEmpty()) {
                            auto ast = parser.parse(fieldData.name, nameState);
                            fieldData.nameParsed = parser.toHtml(ast);
                        }

                        Core::Markdown::ParseState valueState;
                        valueState.isInline = true;
                        if (!fieldData.value.isEmpty()) {
                            auto ast = parser.parse(fieldData.value, valueState);
                            fieldData.valueParsed = parser.toHtml(ast);
                        }

                        data.fields.append(fieldData);
                    }
                }

                // Bot-generated embeds (rich/link/gift/etc.) frequently have
                // no top-level URL; they must still be rendered. Only the
                // url-based image-merge bookkeeping is gated on a URL.
                if (!hasAnything)
                    continue;

                result.append(data);
                if (!embedUrl.isEmpty())
                    urlToEmbedIndex[embedUrl] = result.size() - 1;
            }
        }

        indexEmbedEmojiUrls(msg.id, result);
        // Only cache when images are actually being fetched. The sizeHint pass
        // sets suppressImageFetch, which fills this result with placeholders;
        // caching then would stop the paint pass from ever triggering a real
        // fetch, leaving embed images as gray boxes forever.
        if (!suppressImageFetch)
            embedCache[msg.id] = result;
        return QVariant::fromValue(result);
    }
    case IsPendingRole:
        return msg.nonce.hasValue() && pendingNonces.contains(msg.nonce.get());
    case IsErroredRole:
        return msg.nonce.hasValue() && erroredNonces.contains(msg.nonce.get());
    case UsernameColorRole:
        return resolveAuthorColor(msg.author.get());
    case MessageIdRole:
        return msg.id;
    case ReactionsRole: {
        if (!msg.reactions.hasValue() || msg.reactions->isEmpty())
            return QVariant();

        auto it = reactionCache.find(msg.id);
        if (it != reactionCache.end())
            return QVariant::fromValue(*it);

        QList<ReactionData> data = buildReactionData(msg);
        if (!suppressImageFetch)
            reactionCache.insert(msg.id, data);
        return QVariant::fromValue(data);
    }
    case PollRole: {
        if (!msg.poll.hasValue())
            return QVariant();
        auto it = pollCache.constFind(msg.id);
        if (it != pollCache.constEnd())
            return QVariant::fromValue(*it);
        PollData data = buildPollData(msg);
        pollCache.insert(msg.id, data);
        return QVariant::fromValue(data);
    }
    case StickersRole: {
        if (!msg.stickerItems.hasValue() || msg.stickerItems->isEmpty())
            return QVariant();

        QList<StickerData> result;
        for (const auto &sticker : *msg.stickerItems) {
            StickerData data;
            data.id = sticker.id;
            data.name = sticker.name.get();
            data.formatType = sticker.formatType.get();
            if (data.formatType != Discord::StickerFormatType::Lottie) {
                data.cdnUrl = Discord::Cdn::stickerImage(data.id, data.formatType, 160);
                QSize stickerSize(160, 160);
                data.pixmap = suppressImageFetch
                        ? imageManager->getIfCached(data.cdnUrl, stickerSize)
                        : imageManager->get(data.cdnUrl, stickerSize);
                data.isLoading = !imageManager->isCached(data.cdnUrl, stickerSize);
                data.isAnimated = (data.formatType == Discord::StickerFormatType::APNG ||
                                   data.formatType == Discord::StickerFormatType::GIF);

                // If we have an animated movie, get its current frame
                if (data.isAnimated && !suppressImageFetch) {
                    auto movieIt = stickerMovies.find(data.id);
                    if (movieIt != stickerMovies.end() && *movieIt) {
                        data.currentFrame = (*movieIt)->currentPixmap();
                    } else {
                        // Try to create a movie for this animated sticker
                        // (will fetch data asynchronously and trigger repaint on frame change)
                        const_cast<ChatModel *>(this)->stickerMovie(data.id, data.cdnUrl, data.formatType);
                        auto &rows = stickerMovieRows[data.id];
                        QPersistentModelIndex persistentIndex(index);
                        if (!rows.contains(persistentIndex)) {
                            // Row shifts (history/jump) move persistent indexes
                            // to point at different messages, so `contains` may
                            // miss and the list would grow unbounded. Prune
                            // entries that no longer resolve to this message.
                            for (int i = rows.size() - 1; i >= 0; --i) {
                                const QModelIndex idx = rows[i];
                                if (!idx.isValid()
                                    || idx.data(MessageIdRole).toULongLong()
                                               != static_cast<quint64>(msg.id.get()))
                                    rows.removeAt(i);
                            }
                            rows.append(persistentIndex);
                        }
                    }
                }
            }
            result.append(data);
        }
        return QVariant::fromValue(result);
    }
    case IsSystemMessageRole:
        return isSystemMessageType(msg.type);
    case ReplyDataRole: {
        ReplyData reply;

        if (msg.type != Discord::MessageType::REPLY) {
            reply.state = ReplyData::State::None;
            return QVariant::fromValue(reply);
        }

        if (!msg.referencedMessage) {
            if (msg.referencedMessageNull) {
                reply.state = ReplyData::State::Deleted;
            } else {
                reply.state = ReplyData::State::Unknown;
            }
            if (msg.messageReference.hasValue() && msg.messageReference->messageId.hasValue())
                reply.referencedMessageId = *msg.messageReference->messageId;
            return QVariant::fromValue(reply);
        }

        const auto &ref = msg.referencedMessage;
        reply.state = ReplyData::State::Present;
        reply.referencedMessageId = ref->id;
        reply.authorId = ref->author->id;
        // Clamp the replied-to snippet to a single short line so long
        // original messages can't bloat the reply header (matches Discord).
        QString snippet = ref->content;
        snippet.replace('\n', ' ');
        if (snippet.length() > 100)
            snippet = snippet.left(100) + "...";
        reply.contentSnippet = snippet;

        reply.authorColor = resolveAuthorColor(ref->author.get());
        reply.authorName = resolveAuthorName(ref->author.get());

        return QVariant::fromValue(reply);
    }
    default:
        return {};
    }
}

QList<AttachmentData> ChatModel::buildAttachmentData(const Discord::Message &msg) const
{
    QList<AttachmentData> result;

    if (!msg.attachments.hasValue() || msg.attachments->isEmpty())
        return result;

    const QVector<QPair<qint64, qint64>> *progress = nullptr;
    if (msg.nonce.hasValue()) {
        auto it = uploadProgress.constFind(msg.nonce.get());
        if (it != uploadProgress.constEnd())
            progress = &it.value();
    }

    for (const auto &att : *msg.attachments) {
        AttachmentData data;
        data.id = att.id;
        if (att.proxyUrl.hasValue())
            data.proxyUrl = QUrl(*att.proxyUrl);
        else if (att.url.hasValue())
            data.proxyUrl = QUrl(*att.url);

        if (att.url.hasValue())
            data.originalUrl = QUrl(*att.url);
        else if (att.proxyUrl.hasValue())
            data.originalUrl = QUrl(*att.proxyUrl);

        data.isImage = att.isImage();
        data.filename = att.filename.hasValue() ? *att.filename : QStringLiteral("unknown");
        data.fileSizeBytes = att.size.hasValue() ? *att.size : 0;
        data.isSpoiler = att.isSpoiler();

        // Detect animated attachments: gif and animated webp/avif (klipy media
        // is frequently served as webp despite being a "GIF"). GifAnimation
        // sniffs the real container and falls back to a static render for
        // non-animated payloads, so treating webp as animated-capable is safe.
        bool isGif = false;
        if (att.contentType.hasValue()) {
            const QString ct = *att.contentType;
            isGif = ct == QStringLiteral("image/gif") || ct == QStringLiteral("image/webp") ||
                    ct == QStringLiteral("image/avif");
        }
        if (!isGif && att.filename.hasValue()) {
            const QString name = att.filename->toLower();
            isGif = name.endsWith(QStringLiteral(".gif")) ||
                    name.endsWith(QStringLiteral(".webp")) ||
                    name.endsWith(QStringLiteral(".avif"));
        }
        data.isGif = isGif;

        // Detect video attachments (video/* content type)
        data.isVideo = att.isVideo();

        // Detect voice messages (audio attachments with IS_VOICE_MESSAGE flag)
        bool isVoice = false;
        if (att.contentType.hasValue() && att.contentType->startsWith("audio/") &&
            msg.flags.hasValue() && msg.flags->testFlag(Discord::MessageFlag::IS_VOICE_MESSAGE))
            isVoice = true;
        data.isVoice = isVoice;
        if (isVoice) {
            if (att.waveform.hasValue() && !att.waveform->isEmpty())
                data.waveform = QByteArray::fromBase64(att.waveform->toUtf8());
            if (att.durationSecs.hasValue())
                data.durationSecs = *att.durationSecs;
        }

        int attIndex = result.size();
        if (progress && attIndex < progress->size()) {
            data.uploadSent = (*progress)[attIndex].first;
            data.uploadTotal = (*progress)[attIndex].second;
        }

        if (att.isImage()) {
            QSize original;
            if (att.width.hasValue() && att.height.hasValue())
                original = QSize(*att.width, *att.height);

            data.displaySize = Core::ImageManager::calculateDisplaySize(original);
            if (!att.localPreview.isNull()) {
                // pending paste preview: pixels live in memory, not on disk
                data.pixmap = previewPixmap(att.id, att.localPreview, data.displaySize);
                data.isLoading = false; // decoded synchronously
            } else if (data.proxyUrl.isLocalFile()) {
                // pending dropped-file preview: decoded synchronously from disk
                data.pixmap = localPixmap(data.proxyUrl, data.displaySize);
                data.isLoading = false;
            } else {
                data.pixmap = suppressImageFetch
                                      ? imageManager->getIfCached(data.proxyUrl, data.displaySize)
                                      : imageManager->get(data.proxyUrl, data.displaySize);
                data.isLoading = !imageManager->isCached(data.proxyUrl, data.displaySize);
            }
        } else {
            data.displaySize = QSize();
            data.isLoading = false;
        }

        result.append(data);
    }

    return result;
}

QList<ReactionData> ChatModel::buildReactionData(const Discord::Message &msg) const
{
    QList<ReactionData> result;

    if (!msg.reactions.hasValue() || msg.reactions->isEmpty())
        return result;

    for (const auto &reaction : *msg.reactions) {
        QPixmap emojiPixmap;
        bool isLoading = false;
        Core::Snowflake emojiId;
        if (reaction.emoji.hasValue() && !reaction.emoji->isUnicode()) {
            emojiId = reaction.emoji->id;
            QString emojiUrl = reaction.emoji->getImageUrl(48);
            QSize emojiSize(16, 16);
            emojiPixmap = imageManager->get(QUrl(emojiUrl), emojiSize);
            isLoading = !imageManager->isCached(QUrl(emojiUrl), emojiSize);
        }

        int normalCount = reaction.countDetails.hasValue() ? *reaction.countDetails->normal : *reaction.count;
        int burstCount = reaction.countDetails.hasValue() ? *reaction.countDetails->burst : 0;

        if (burstCount > 0) {
            ReactionData data;
            data.emojiName = reaction.emoji.hasValue() ? reaction.emoji->name.get() : QString();
            data.emojiId = emojiId;
            data.emojiAnimated = reaction.emoji.hasValue() &&
                                 reaction.emoji->animated.hasValue() &&
                                 *reaction.emoji->animated;
            data.count = burstCount;
            data.me = reaction.meBurst.hasValue() && *reaction.meBurst;
            data.isBurst = true;
            data.emojiPixmap = emojiPixmap;
            data.isLoading = isLoading;
            data.burstTintColor = reaction.getBrightestBurstColor();
            result.append(data);
        }

        if (normalCount > 0) {
            ReactionData data;
            data.emojiName = reaction.emoji.hasValue() ? reaction.emoji->name.get() : QString();
            data.emojiId = emojiId;
            data.emojiAnimated = reaction.emoji.hasValue() &&
                                 reaction.emoji->animated.hasValue() &&
                                 *reaction.emoji->animated;
            data.count = normalCount;
            data.me = reaction.me;
            data.isBurst = false;
            data.emojiPixmap = emojiPixmap;
            data.isLoading = isLoading;
            result.append(data);
        }
    }

    return result;
}

PollData ChatModel::buildPollData(const Discord::Message &msg) const
{
    PollData data;
    if (!msg.poll.hasValue())
        return data;

    const Discord::Poll &poll = *msg.poll;

    if (poll.question.hasValue() && poll.question->text.hasValue())
        data.question = poll.question->text.get();

    data.allowMultiselect = poll.allowMultiselect.hasValue() && *poll.allowMultiselect;

    if (poll.results.hasValue()) {
        const Discord::PollResults &results = *poll.results;
        data.isFinalized = results.isFinalized.hasValue() && *results.isFinalized;
        if (results.answerCounts.hasValue()) {
            for (const auto &count : *results.answerCounts)
                data.totalVotes += count.count.hasValue() ? *count.count : 0;
        }
    }

    data.isExpired = poll.isExpired();

    if (poll.myAnswers.hasValue())
        data.myAnswers = poll.myAnswers.get();

    if (poll.answers.hasValue()) {
        for (const auto &answer : *poll.answers) {
            PollAnswerData answerData;
            answerData.id = answer.answerId.hasValue() ? *answer.answerId : 0;

            if (answer.pollMedia.hasValue()) {
                answerData.text = answer.pollMedia->text.hasValue() ? *answer.pollMedia->text
                                                                    : QString();
                const auto &emoji = answer.pollMedia->emoji;
                // A default-constructed Emoji reports a (default) id, so only
                // trust it when the object actually carried an id.
                if (emoji.hasValue() && !emoji.isNull() && emoji->name.hasValue()) {
                    answerData.emojiName = emoji->name.get();
                    if (!emoji->isUnicode() && emoji->id.hasValue() && emoji->id->isValid())
                        answerData.emojiId = emoji->id.get();
                }
            }

            if (poll.results.hasValue() && poll.results->answerCounts.hasValue()) {
                for (const auto &count : *poll.results->answerCounts) {
                    if (count.id.hasValue() && *count.id == answerData.id) {
                        answerData.count = count.count.hasValue() ? *count.count : 0;
                        break;
                    }
                }
            }

            if (data.totalVotes > 0)
                answerData.percent = double(answerData.count) * 100.0 / double(data.totalVotes);

            answerData.me = data.myAnswers.contains(answerData.id);
            data.answers.append(answerData);
        }
    }

    return data;
}

bool ChatModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    if (role == CachedSizeRole) {
        setCachedSize(index, value.toSize());
        return true;
    }

    return false;
}

void ChatModel::setCachedSize(const QModelIndex &index, const QSize &size) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= messages.size())
        return;
    sizeCache[messages[index.row()].id] = size;
}

Snowflake ChatModel::getOldestMessageId() const
{
    if (messages.isEmpty())
        return Snowflake::Invalid;
    return messages.first().id;
}

Snowflake ChatModel::getActiveChannelId() const
{
    return currentChannelId;
}

Snowflake ChatModel::getActiveGuildId() const
{
    return currentGuildId;
}

void ChatModel::handleIncomingMessages(const Core::MessageRequestResult &result)
{
    if (!result.success)
        return;

    if (result.channelId != currentChannelId)
        return;

    if (result.messages.isEmpty())
        return;

    QVector<Discord::Message> incomingMessages{ result.messages.cbegin(), result.messages.cend() };
    std::sort(incomingMessages.begin(), incomingMessages.end(),
              [](const Discord::Message &a, const Discord::Message &b) {
                  return a.id.get() < b.id.get();
              });

    switch (result.type) {
    case Discord::Client::MessageLoadType::Latest: {
        beginResetModel();
        sizeCache.clear();
        attachmentCache.clear();
        embedCache.clear();
        reactionCache.clear();
        pollCache.clear();
        docCache.clear();
        pendingNonces.clear();
        erroredNonces.clear();
        uploadProgress.clear();
        localPixmapCache.clear();
        previewPixmapCache.clear();
        emojiUrlIndex.clear();
        emojiUrlsByMessage.clear();
        messageRowIndexDirty = true;

        // Clean up animated sticker movies from previous message set
        qDeleteAll(stickerMovies);
        stickerMovies.clear();
        stickerMovieRows.clear();

        // Clean up GIF attachment animations from previous message set
        for (auto it = gifAttachmentAnimations.begin(); it != gifAttachmentAnimations.end(); ++it) {
            auto *anim = it.value();
            if (anim) {
                anim->pause();
                disconnect(anim, nullptr, this, nullptr);
                imageManager->releaseGifAnimation(it.key());
            }
        }
        gifAttachmentAnimations.clear();
        gifAnimationRows.clear();
        failedGifUrls.clear();

        // Allow images that failed in the previous channel to be retried here.
        imageManager->clearFailedRequests();

        messages = incomingMessages;
        for (const auto &msg : messages)
            indexMessageEmojiUrls(msg);
        endResetModel();
        trimOldestMessagesIfNeeded();
        break;
    };
    case Discord::Client::MessageLoadType::History: {
        if (incomingMessages.isEmpty())
            break;

        if (messages.isEmpty()) {
            // History load before any Latest — treat as initial population
            beginInsertRows({}, 0, incomingMessages.size() - 1);
            messages = incomingMessages;
            for (const auto &msg : messages)
                indexMessageEmojiUrls(msg);
            messageRowIndexDirty = true;
            endInsertRows();
            break;
        }

        const Snowflake oldAnchorId = messages.first().id;

        // Deduplicate: filter out any incomingMessages whose IDs already exist
        QSet<quint64> existingIds;
        existingIds.reserve(messages.size());
        for (const auto &msg : messages)
            existingIds.insert(msg.id.get());

        QVector<Discord::Message> trulyNew;
        trulyNew.reserve(incomingMessages.size());
        for (const auto &msg : incomingMessages) {
            if (!existingIds.contains(msg.id.get()))
                trulyNew.append(msg);
        }

        if (trulyNew.isEmpty())
            break;

        int numNew = trulyNew.size();

        beginInsertRows({}, 0, numNew - 1);
        messages = trulyNew + messages;
        for (const auto &msg : trulyNew)
            indexMessageEmojiUrls(msg);
        messageRowIndexDirty = true;
        endInsertRows();

        // invalidate cached size cuz header and/or separator might have moved
        sizeCache.remove(oldAnchorId);
        QModelIndex oldAnchorIdx = index(numNew, 0);
        emit dataChanged(oldAnchorIdx, oldAnchorIdx,
                         { CachedSizeRole, ShowHeaderRole, DateSeparatorRole });

        break;
    }
    case Discord::Client::MessageLoadType::Jump: {
        // Insert the fetched jump-target message into the existing history,
        // preserving ascending ID order so it can be centered by the view.
        if (incomingMessages.isEmpty())
            break;

        QSet<quint64> existingIds;
        existingIds.reserve(messages.size());
        for (const auto &msg : messages)
            existingIds.insert(msg.id.get());

        QVector<Discord::Message> trulyNew;
        trulyNew.reserve(incomingMessages.size());
        for (const auto &msg : incomingMessages) {
            if (!existingIds.contains(msg.id.get()))
                trulyNew.append(msg);
        }

        if (trulyNew.isEmpty())
            break;

        int firstAffected = static_cast<int>(messages.size());
        int lastAffected = -1;

        for (const auto &msg : trulyNew) {
            const Snowflake targetId = msg.id.get();
            auto it = std::lower_bound(messages.begin(), messages.end(), targetId,
                                       [](const Discord::Message &m, const Snowflake &id) {
                                           return m.id.get() < id;
                                       });
            const int row = static_cast<int>(std::distance(messages.begin(), it));
            beginInsertRows({}, row, row);
            messages.insert(it, msg);
            indexMessageEmojiUrls(msg);
            messageRowIndexDirty = true;
            endInsertRows();

            firstAffected = qMin(firstAffected, row);
            lastAffected = qMax(lastAffected, qMin(row + 1, static_cast<int>(messages.size()) - 1));
        }

        if (firstAffected <= lastAffected) {
            for (int row = firstAffected; row <= lastAffected; ++row)
                sizeCache.remove(messages[row].id);

            emit dataChanged(index(firstAffected, 0), index(lastAffected, 0),
                             { CachedSizeRole, ShowHeaderRole, DateSeparatorRole });
        }

        trimOldestMessagesIfNeeded();
        break;
    }
    case Discord::Client::MessageLoadType::Updated: {
        // Edits and reaction updates must refresh the existing row in place —
        // never insert a new one.
        ensureMessageRowIndex();
        for (const auto &incomingMsg : incomingMessages) {
            const int row = messageRowById.value(incomingMsg.id, -1);
            if (row < 0)
                continue;

            unindexMessageEmojiUrls(messages[row].id);
            messages[row] = incomingMsg;
            indexMessageEmojiUrls(messages[row]);

            sizeCache.remove(incomingMsg.id);
            attachmentCache.remove(incomingMsg.id);
            embedCache.remove(incomingMsg.id);
            reactionCache.remove(incomingMsg.id);
            pollCache.remove(incomingMsg.id);
            invalidateDocCacheForMessage(incomingMsg.id);

            QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx);
        }
        break;
    }
    case Discord::Client::MessageLoadType::Created: {
        // Track which incoming messages have been handled (by ID match or nonce
        // replacement) so they aren't also appended as new messages below.
        // Processing each message independently is critical: a single batch can
        // contain both message updates (edits) AND new messages, and we must not
        // drop either.
        ensureMessageRowIndex();
        QVector<bool> handled(incomingMessages.size(), false);

        // Phase 1 — update existing messages by matching ID
        for (int j = 0; j < incomingMessages.size(); j++) {
            const auto &incomingMsg = incomingMessages[j];
            const int row = messageRowById.value(incomingMsg.id, -1);
            if (row < 0)
                continue;

            unindexMessageEmojiUrls(messages[row].id);
            messages[row] = incomingMsg;
            indexMessageEmojiUrls(messages[row]);

            sizeCache.remove(incomingMsg.id);
            attachmentCache.remove(incomingMsg.id);
            embedCache.remove(incomingMsg.id);
            reactionCache.remove(incomingMsg.id);
            pollCache.remove(incomingMsg.id);
            invalidateDocCacheForMessage(incomingMsg.id);

            QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx);
            handled[j] = true;
        }

        // Phase 2 — replace sent messages by nonce (skip ones already handled)
        QHash<QString, int> nonceToRow;
        nonceToRow.reserve(messages.size());
        for (int i = 0; i < messages.size(); ++i) {
            if (messages[i].nonce.hasValue())
                nonceToRow.insert(messages[i].nonce.get(), i);
        }

        for (int j = 0; j < incomingMessages.size(); j++) {
            if (handled[j])
                continue;

            const auto &incomingMsg = incomingMessages[j];
            if (!incomingMsg.nonce.hasValue())
                continue;

            QString nonce = incomingMsg.nonce.get();
            auto it = nonceToRow.find(nonce);
            if (it == nonceToRow.end())
                continue;

            const int row = it.value();
            prunePreviewCaches(messages[row]); // drop the pending preview's pixmaps
            const Snowflake oldId = messages[row].id;
            unindexMessageEmojiUrls(oldId);
            messages[row] = incomingMsg;
            indexMessageEmojiUrls(messages[row]);
            pendingNonces.remove(nonce);
            uploadProgress.remove(nonce);
            // Drop caches keyed by the old placeholder id — they can never be
            // hit again after the ID changes and would leak for the session.
            sizeCache.remove(oldId);
            attachmentCache.remove(oldId);
            embedCache.remove(oldId);
            reactionCache.remove(oldId);
            pollCache.remove(oldId);
            invalidateDocCacheForMessage(oldId);
            reactionCache.remove(incomingMsg.id);
            pollCache.remove(incomingMsg.id);
            attachmentCache.remove(incomingMsg.id);
            messageRowIndexDirty = true; // message ID changed
            QModelIndex idx = index(row, 0);
            emit dataChanged(idx, idx);
            handled[j] = true;
        }

        // Phase 3 — append truly new messages (those not matched by ID or nonce).
        // IsPendingRole is driven by pendingNonces, which is pre-seeded by
        // messageSendPending; a pending-outbound message from another session
        // will not be registered and therefore won't show as a stuck local send.
        QVector<Discord::Message> newMessages;
        for (int j = 0; j < incomingMessages.size(); j++) {
            if (handled[j])
                continue;

            newMessages.append(incomingMessages[j]);
        }

        if (!newMessages.isEmpty()) {
            beginInsertRows({}, messages.size(), messages.size() + newMessages.size() - 1);

            messages = messages + newMessages;
            for (const auto &msg : newMessages)
                indexMessageEmojiUrls(msg);
            messageRowIndexDirty = true;
            endInsertRows();
            trimOldestMessagesIfNeeded();

            // Track newly inserted message IDs for highlight animation
            for (const auto &msg : newMessages)
                newMessageIds.insert(msg.id.get());

            // Schedule delayed clear of the new-message highlight so the delegate
            // can paint a brief background flash. Emit per-row changes instead of
            // a role-less full-range dataChanged, which would make the view
            // re-query every role and repaint the whole list.
            QTimer::singleShot(2500, this, [this, ids = newMessages]() {
                if (messages.isEmpty())
                    return;
                ensureMessageRowIndex();
                for (const auto &msg : ids) {
                    if (!newMessageIds.remove(msg.id.get()))
                        continue;
                    const int row = messageRowById.value(msg.id.get(), -1);
                    if (row >= 0)
                        emit dataChanged(index(row, 0), index(row, 0));
                }
            });
        }
        break;
    }
    default:
        break;
    }
}

void ChatModel::handleMessageDeleted(Snowflake channelId, Snowflake messageId)
{
    if (channelId != currentChannelId)
        return;

    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].id == messageId) {
            if (messages[i].nonce.hasValue()) {
                pendingNonces.remove(messages[i].nonce.get());
                uploadProgress.remove(messages[i].nonce.get());
            }
            prunePreviewCaches(messages[i]); // cancelled/deleted preview won't render again
            beginRemoveRows({}, i, i);
            sizeCache.remove(messageId);
            attachmentCache.remove(messageId);
            embedCache.remove(messageId);
            reactionCache.remove(messageId);
            pollCache.remove(messageId);
            invalidateDocCacheForMessage(messageId);
            unindexMessageEmojiUrls(messageId);
            messages.remove(i);
            messageRowIndexDirty = true;
            endRemoveRows();

            // invalidate what came afterwards
            if (i < messages.size()) {
                const auto &nextMessage = messages[i];

                sizeCache.remove(nextMessage.id);
                attachmentCache.remove(nextMessage.id);
                embedCache.remove(nextMessage.id);
                invalidateDocCacheForMessage(nextMessage.id);

                QModelIndex idx = index(i, 0);
                emit dataChanged(idx, idx, { CachedSizeRole, ShowHeaderRole, DateSeparatorRole });
            }
            break;
        }
    }
}

void ChatModel::handleMessageErrored(const QString &nonce)
{
    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].nonce.hasValue() && messages[i].nonce.get() == nonce) {
            pendingNonces.remove(nonce);
            erroredNonces.insert(nonce);
            uploadProgress.remove(nonce);
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx);
            break;
        }
    }
}

void ChatModel::addPendingNonce(const QString &nonce)
{
    pendingNonces.insert(nonce);
}

void ChatModel::handleUploadProgress(const QString &nonce, int fileIndex, qint64 sent, qint64 total)
{
    if (fileIndex < 0 || !pendingNonces.contains(nonce))
        return;

    auto &progress = uploadProgress[nonce];
    while (progress.size() <= fileIndex)
        progress.append({ -1, -1 });
    progress[fileIndex] = { sent, total };

    for (int i = 0; i < messages.size(); i++) {
        if (messages[i].nonce.hasValue() && messages[i].nonce.get() == nonce) {
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, { AttachmentsRole });
            break;
        }
    }
}

QPixmap ChatModel::localPixmap(const QUrl &url, const QSize &displaySize) const
{
    // Keyed by path + size so repeat paints for the same attachment at the
    // same display size don't re-decode/re-scale from disk.
    const QString cacheKey = url.toLocalFile() + u'|' + QString::number(displaySize.width()) +
                             u'x' + QString::number(displaySize.height());
    auto it = localPixmapCache.constFind(cacheKey);
    if (it != localPixmapCache.constEnd())
        return it.value();

    QImageReader reader(url.toLocalFile());
    reader.setAutoTransform(true);
    QSize original = reader.size();
    if (original.isValid() && displaySize.isValid()) {
        QSize scaled = original.scaled(displaySize * qApp->devicePixelRatio(),
                                       Qt::KeepAspectRatio);
        if (scaled.width() < original.width())
            reader.setScaledSize(scaled);
    }

    QPixmap pixmap = QPixmap::fromImage(reader.read());
    if (!pixmap.isNull()) {
        pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
        localPixmapCache.insert(cacheKey, pixmap);
    }
    return pixmap;
}

QPixmap ChatModel::previewPixmap(Snowflake attachmentId, const QImage &image,
                                 const QSize &displaySize) const
{
    auto it = previewPixmapCache.constFind(attachmentId);
    if (it != previewPixmapCache.constEnd())
        return it.value();

    qreal dpr = qApp->devicePixelRatio();
    QImage scaled = displaySize.isValid()
                            ? image.scaled(displaySize * dpr, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation)
                            : image;
    QPixmap pixmap = QPixmap::fromImage(scaled);
    if (!pixmap.isNull())
        pixmap.setDevicePixelRatio(dpr);
    previewPixmapCache.insert(attachmentId, pixmap);
    return pixmap;
}

void ChatModel::prunePreviewCaches(const Discord::Message &msg)
{
    if (!msg.attachments.hasValue())
        return;
    for (const auto &att : *msg.attachments) {
        previewPixmapCache.remove(*att.id);
        if (att.proxyUrl.hasValue()) {
            // localPixmapCache is keyed by path + size; drop every size variant
            const QString prefix = QUrl(*att.proxyUrl).toLocalFile() + u'|';
            for (auto it = localPixmapCache.begin(); it != localPixmapCache.end();) {
                if (it.key().startsWith(prefix))
                    it = localPixmapCache.erase(it);
                else
                    ++it;
            }
        }
    }
}

static void collectImgSrcUrls(const QString &html, QSet<QString> &out)
{
    if (html.isEmpty())
        return;
    static const QRegularExpression imgSrcRe(QStringLiteral(R"rx(src="([^"]+)")rx"));
    auto it = imgSrcRe.globalMatch(html);
    while (it.hasNext())
        out.insert(it.next().captured(1));
}

void ChatModel::indexMessageEmojiUrls(const Discord::Message &msg)
{
    QSet<QString> contentUrls;
    collectImgSrcUrls(msg.parsedContentCached, contentUrls);

    // Embeds are parsed lazily; index whatever is already cached. When the
    // EmbedsRole later parses and caches, indexEmbedEmojiUrls picks up the rest.
    if (embedCache.contains(msg.id)) {
        for (const auto &embed : embedCache.value(msg.id)) {
            collectImgSrcUrls(embed.titleParsed, contentUrls);
            collectImgSrcUrls(embed.descriptionParsed, contentUrls);
            for (const auto &field : embed.fields) {
                collectImgSrcUrls(field.nameParsed, contentUrls);
                collectImgSrcUrls(field.valueParsed, contentUrls);
            }
        }
    }

    QSet<QString> reactionUrls;
    if (msg.reactions.hasValue()) {
        for (const auto &reaction : *msg.reactions) {
            if (reaction.emoji.hasValue() && !reaction.emoji->isUnicode())
                reactionUrls.insert(reaction.emoji->getImageUrl(48));
        }
    }

    QSet<QString> stickerUrls;
    if (msg.stickerItems.hasValue()) {
        for (const auto &sticker : *msg.stickerItems) {
            stickerUrls.insert(Discord::Cdn::stickerImage(sticker.id, sticker.formatType.get(), 160)
                                       .toString());
        }
    }

    QSet<QString> imageUrls;
    if (msg.attachments.hasValue()) {
        for (const auto &att : *msg.attachments) {
            if (att.proxyUrl.hasValue())
                imageUrls.insert(*att.proxyUrl);
        }
    }
    if (msg.embeds.hasValue()) {
        for (const auto &embed : *msg.embeds) {
            if (embed.author.hasValue() && embed.author->proxyIconUrl.hasValue())
                imageUrls.insert(*embed.author->proxyIconUrl);
            if (embed.footer.hasValue() && embed.footer->proxyIconUrl.hasValue())
                imageUrls.insert(*embed.footer->proxyIconUrl);
            if (embed.thumbnail.hasValue() && embed.thumbnail->proxyUrl.hasValue())
                imageUrls.insert(*embed.thumbnail->proxyUrl);
            if (embed.image.hasValue() && embed.image->proxyUrl.hasValue())
                imageUrls.insert(*embed.image->proxyUrl);
            if (embed.video.hasValue() && embed.video->proxyUrl.hasValue())
                imageUrls.insert(*embed.video->proxyUrl);
        }
    }

    QSet<QString> &all = emojiUrlsByMessage[msg.id];
    all = contentUrls + reactionUrls + stickerUrls + imageUrls;
    for (const QString &url : contentUrls)
        emojiUrlIndex[url].content.insert(msg.id);
    for (const QString &url : reactionUrls)
        emojiUrlIndex[url].reactions.insert(msg.id);
    for (const QString &url : stickerUrls)
        emojiUrlIndex[url].stickers.insert(msg.id);
    for (const QString &url : imageUrls)
        emojiUrlIndex[url].images.insert(msg.id);
}

void ChatModel::indexEmbedEmojiUrls(Snowflake messageId, const QList<EmbedData> &embeds) const
{
    QSet<QString> embedUrls;
    for (const auto &embed : embeds) {
        collectImgSrcUrls(embed.titleParsed, embedUrls);
        collectImgSrcUrls(embed.descriptionParsed, embedUrls);
        for (const auto &field : embed.fields) {
            collectImgSrcUrls(field.nameParsed, embedUrls);
            collectImgSrcUrls(field.valueParsed, embedUrls);
        }
    }

    auto &urls = const_cast<ChatModel *>(this)->emojiUrlsByMessage[messageId];
    for (const QString &url : embedUrls) {
        urls.insert(url);
        const_cast<ChatModel *>(this)->emojiUrlIndex[url].content.insert(messageId);
    }
}

void ChatModel::unindexMessageEmojiUrls(Snowflake messageId)
{
    auto it = emojiUrlsByMessage.find(messageId);
    if (it == emojiUrlsByMessage.end())
        return;

    for (const QString &url : it.value()) {
        auto refIt = emojiUrlIndex.find(url);
        if (refIt == emojiUrlIndex.end())
            continue;
        refIt->content.remove(messageId);
        refIt->reactions.remove(messageId);
        refIt->stickers.remove(messageId);
        refIt->images.remove(messageId);
        if (refIt->content.isEmpty() && refIt->reactions.isEmpty() &&
            refIt->stickers.isEmpty() && refIt->images.isEmpty())
            emojiUrlIndex.erase(refIt);
    }
    emojiUrlsByMessage.erase(it);
}

int ChatModel::rowForMessage(Snowflake messageId) const
{
    if (!messageId.isValid())
        return -1;
    ensureMessageRowIndex();
    return messageRowById.value(messageId, -1);
}

QList<ChatModel::MessageSearchHit> ChatModel::searchLoadedMessages(const QString &query,
                                                                   int limit) const
{
    QList<MessageSearchHit> hits;
    const QString needle = query.trimmed().toCaseFolded();
    if (needle.isEmpty() || limit <= 0)
        return hits;

    for (int row = 0; row < messages.size() && hits.size() < limit; ++row) {
        const Discord::Message &msg = messages.at(row);
        if (!msg.content.hasValue())
            continue;
        if (!msg.content.get().toCaseFolded().contains(needle))
            continue;

        MessageSearchHit hit;
        hit.messageId = msg.id.get();
        const Discord::User author = msg.author.getOr(Discord::User{});
        hit.authorName = resolveAuthorName(author);
        hit.authorId = author.id.getOr(Core::Snowflake::Invalid);
        hit.authorAvatarHash = author.avatar.getOr(QString());
        hit.content = msg.content.get();
        if (msg.timestamp.hasValue())
            hit.timestampSecs = msg.timestamp.get().toSecsSinceEpoch();
        hit.row = row;
        hits.append(hit);
    }
    return hits;
}

void ChatModel::ensureMessageRowIndex() const
{
    if (!messageRowIndexDirty)
        return;
    messageRowById.clear();
    messageRowById.reserve(messages.size());
    for (int row = 0; row < messages.size(); ++row)
        messageRowById.insert(messages[row].id, row);
    messageRowIndexDirty = false;
}

void ChatModel::trimOldestMessagesIfNeeded()
{
    const int excess = messages.size() - MaxLoadedMessages;
    if (excess <= 0)
        return;

    beginRemoveRows({}, 0, excess - 1);
    for (int i = 0; i < excess; ++i) {
        const auto &msg = messages[i];
        if (msg.nonce.hasValue()) {
            pendingNonces.remove(msg.nonce.get());
            erroredNonces.remove(msg.nonce.get());
            uploadProgress.remove(msg.nonce.get());
        }
        prunePreviewCaches(msg);
        sizeCache.remove(msg.id);
        attachmentCache.remove(msg.id);
        embedCache.remove(msg.id);
        reactionCache.remove(msg.id);
        pollCache.remove(msg.id);
        invalidateDocCacheForMessage(msg.id);
        unindexMessageEmojiUrls(msg.id);
        newMessageIds.remove(msg.id.get());
    }
    messages.remove(0, excess);
    messageRowIndexDirty = true;
    endRemoveRows();
}

void ChatModel::setActiveChannel(Snowflake channelId, Snowflake guildId)
{
    if (currentChannelId == channelId)
        return;

    imageManager->unpinGroup(Core::PinGroup::ChatView);

    currentChannelId = channelId;
    currentGuildId = guildId;

    // Clean up animated sticker movies to prevent memory leaks on channel switch
    qDeleteAll(stickerMovies);
    stickerMovies.clear();
    stickerMovieRows.clear();

    // Clean up GIF attachment animations
    for (auto it = gifAttachmentAnimations.begin(); it != gifAttachmentAnimations.end(); ++it) {
        auto *anim = it.value();
        if (anim) {
            anim->pause();
            disconnect(anim, nullptr, this, nullptr);
            imageManager->releaseGifAnimation(it.key());
        }
    }
    gifAttachmentAnimations.clear();
    gifAnimationRows.clear();
    failedGifUrls.clear();

    // Channel switch: give previously failed image fetches a fresh chance.
    imageManager->clearFailedRequests();
    beginResetModel();
    messages.clear();
    sizeCache.clear();
    attachmentCache.clear();
    embedCache.clear();
    reactionCache.clear();
    pollCache.clear();
    docCache.clear();
    pendingNonces.clear();
    erroredNonces.clear();
    uploadProgress.clear();
    localPixmapCache.clear();
    previewPixmapCache.clear();
    revealedSpoilers.clear();
    emojiUrlIndex.clear();
    emojiUrlsByMessage.clear();
    messageRowIndexDirty = true;
    endResetModel();
}

void ChatModel::refreshUsersInView(const QList<Snowflake> &userIds)
{
    bool refreshAll = userIds.isEmpty();

    int firstRow = -1;
    int lastRow = -1;

    for (int row = 0; row < messages.size(); ++row) {
        const auto &msg = messages[row];
        if (!msg.author.hasValue())
            continue;

        Snowflake authorId = msg.author->id.get();

        if (refreshAll || userIds.contains(authorId)) {
            if (firstRow < 0)
                firstRow = row;
            lastRow = row;
        }
    }

    if (firstRow >= 0) {
        emit dataChanged(index(firstRow, 0), index(lastRow, 0),
                         { UsernameRole, UsernameColorRole });
    }
}

void ChatModel::revealSpoiler(Snowflake attachmentId)
{
    if (revealedSpoilers.contains(attachmentId))
        return;

    revealedSpoilers.insert(attachmentId);

    for (int row = 0; row < messages.size(); ++row) {
        const auto &msg = messages[row];
        if (msg.attachments.hasValue()) {
            for (const auto &att : *msg.attachments) {
                if (*att.id == attachmentId) {
                    QModelIndex idx = index(row, 0);
                    emit dataChanged(idx, idx, { AttachmentsRole, CachedSizeRole });
                    return;
                }
            }
        }
    }
}

bool ChatModel::isSpoilerRevealed(Snowflake attachmentId) const
{
    return revealedSpoilers.contains(attachmentId);
}

QTextDocument *ChatModel::getCachedDocument(const DocCacheKey &key) const
{
    return docCache.object(key);
}

void ChatModel::cacheDocument(const DocCacheKey &key, QTextDocument *doc) const
{
    docCache.insert(key, doc);
    docCacheSubIds[key.messageId].insert(key.subId);
}

void ChatModel::invalidateDocCache()
{
    docCache.clear();
    docCacheSubIds.clear();
}

void ChatModel::invalidateLayout()
{
    invalidateDocCache();
    sizeCache.clear();
}

void ChatModel::invalidateDocCacheForMessage(Snowflake messageId)
{
    auto it = docCacheSubIds.find(messageId);
    if (it == docCacheSubIds.end())
        return;

    for (int subId : it.value())
        docCache.remove(DocCacheKey{ messageId, subId });
    docCacheSubIds.erase(it);
}

QMovie *ChatModel::stickerMovie(Snowflake stickerId, const QUrl &cdnUrl,
                                Discord::StickerFormatType formatType)
{
    // Only APNG and GIF formats can be animated
    if (formatType != Discord::StickerFormatType::APNG &&
        formatType != Discord::StickerFormatType::GIF) {
        return nullptr;
    }

    auto it = stickerMovies.find(stickerId);
    if (it != stickerMovies.end()) {
        if (*it) {
            (*it)->start();
            return *it;
        }
        stickerMovies.erase(it);
    }

    // Need to fetch the sticker data and create a QMovie from it
    // Use ImageManager to get the raw data, then create QMovie from it
    auto *movie = new QMovie(this);
    movie->setCacheMode(QMovie::CacheAll);

    // Fetch the sticker image data from CDN via a shared NAM (a per-sticker
    // NAM would leak whenever a reply stalls and never finishes).
    if (!stickerNetworkManager)
        stickerNetworkManager = new QNetworkAccessManager(this);
    QNetworkRequest request(cdnUrl);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = stickerNetworkManager->get(request);
    // Capture the movie via QPointer: qDeleteAll() on channel switch/reset
    // deletes movies whose fetch replies may still be in flight. QPointer
    // auto-nulls, so a late finished() cannot touch a freed QMovie.
    QPointer<QMovie> movieGuard(movie);
    connect(reply, &QNetworkReply::finished, this, [this, reply, stickerId, movieGuard]() {
        reply->deleteLater();

        QMovie *movie = movieGuard.data();
        if (!movie)
            return; // movie was deleted by a channel switch/reset

        if (reply->error() != QNetworkReply::NoError) {
            if (stickerMovies.value(stickerId) == movie)
                stickerMovies.remove(stickerId);
            movie->deleteLater();
            return;
        }

        // A newer fetch for the same sticker may have replaced this movie.
        if (stickerMovies.value(stickerId) != movie) {
            movie->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        QBuffer *buffer = new QBuffer(movie);
        buffer->setData(data);
        buffer->open(QIODevice::ReadOnly);
        movie->setDevice(buffer);

        if (!movie->isValid() || movie->frameCount() <= 1) {
            if (stickerMovies.value(stickerId) == movie)
                stickerMovies.remove(stickerId);
            movie->deleteLater();
            return;
        }

        movie->start();
        connect(movie, &QMovie::frameChanged, this, &ChatModel::onStickerFrameChanged);
    });

    stickerMovies.insert(stickerId, movie);
    return movie;
}

void ChatModel::onStickerFrameChanged()
{
    auto *movie = qobject_cast<QMovie *>(sender());
    if (!movie)
        return;

    // Find the sticker ID for this movie and trigger a repaint of its row
    for (auto it = stickerMovies.begin(); it != stickerMovies.end(); ++it) {
        if (*it == movie) {
            auto rowIt = stickerMovieRows.find(it.key());
            if (rowIt != stickerMovieRows.end()) {
                for (auto rowIndexIt = rowIt->begin(); rowIndexIt != rowIt->end();) {
                    if (!rowIndexIt->isValid()) {
                        rowIndexIt = rowIt->erase(rowIndexIt);
                        continue;
                    }
                    QModelIndex idx = *rowIndexIt;
                    emit dataChanged(idx, idx, { StickersRole });
                    ++rowIndexIt;
                }
            }
            return;
        }
    }
}

void ChatModel::cleanupStickerMovies(const QList<Snowflake> &visibleStickerIds)
{
    QSet<Snowflake> visible(visibleStickerIds.begin(), visibleStickerIds.end());

    for (auto it = stickerMovies.begin(); it != stickerMovies.end();) {
        if (!visible.contains(it.key())) {
            const Snowflake stickerId = it.key();
            QMovie *movie = it.value();
            movie->stop();
            disconnect(movie, nullptr, this, nullptr);
            movie->deleteLater();
            it = stickerMovies.erase(it);
            stickerMovieRows.remove(stickerId);
        } else {
            ++it;
        }
    }
}

bool ChatModel::hasImageFailed(const QUrl &url, const QSize &size) const
{
    return imageManager->hasFailed(url, size);
}

Core::GifAnimation *ChatModel::ensureGifAnimation(const QUrl &url, int row) const
{
    if (!url.isValid())
        return nullptr;

    // A GIF that already failed to load/decode won't recover; remember it and
    // fall back to the static pixmap instead of re-downloading on every paint.
    if (failedGifUrls.contains(url))
        return nullptr;

    // Return existing animation if already created
    auto it = gifAttachmentAnimations.constFind(url);
    if (it != gifAttachmentAnimations.constEnd() && it.value()) {
        auto &rows = gifAnimationRows[url];
        QPersistentModelIndex persistentIndex(index(row, 0));
        if (!rows.contains(persistentIndex)) {
            // Row shifts move persistent indexes to different messages; prune
            // stale entries so the row list does not grow unbounded.
            const quint64 msgId =
                    row < messages.size() ? static_cast<quint64>(messages[row].id.get()) : quint64(0);
            for (int i = rows.size() - 1; i >= 0; --i) {
                const QModelIndex idx = rows[i];
                if (!idx.isValid() || idx.data(MessageIdRole).toULongLong() != msgId)
                    rows.removeAt(i);
            }
            rows.append(persistentIndex);
        }
        it.value()->play();
        return it.value();
    }

    // Create new GIF animation (auto-managed by ImageManager)
    Core::GifAnimation *anim = imageManager->createGifAnimation(url);
    if (!anim)
        return nullptr;

    gifAttachmentAnimations.insert(url, anim);
    gifAnimationRows[url] = { QPersistentModelIndex(index(row, 0)) };

    // When the animation's frame changes, emit dataChanged to trigger repainting
    auto *self = const_cast<ChatModel *>(this);
    connect(anim, &Core::GifAnimation::frameChanged, this, [self, url, anim]() {
        auto rowIt = self->gifAnimationRows.constFind(url);
        if (rowIt != self->gifAnimationRows.constEnd()) {
            for (const auto &rowIndex : rowIt.value()) {
                if (!rowIndex.isValid())
                    continue;
                QModelIndex idx = rowIndex;
                emit self->dataChanged(idx, idx, { AttachmentsRole, EmbedsRole });
            }
        }
    });

    connect(anim, &Core::GifAnimation::ready, this, [self, url]() {
        auto rowIt = self->gifAnimationRows.constFind(url);
        if (rowIt != self->gifAnimationRows.constEnd()) {
            for (const auto &rowIndex : rowIt.value()) {
                if (!rowIndex.isValid())
                    continue;
                QModelIndex idx = rowIndex;
                emit self->dataChanged(idx, idx, { AttachmentsRole, EmbedsRole, CachedSizeRole });
            }
        }
    });

    connect(anim, &Core::GifAnimation::failed, this, [self, url, anim]() {
        if (self->gifAttachmentAnimations.value(url) != anim)
            return;
        self->failedGifUrls.insert(url);
        self->gifAttachmentAnimations.remove(url);
        self->gifAnimationRows.remove(url);
        self->imageManager->releaseGifAnimation(url);
    });

    // Start playing
    anim->play();

    return anim;
}

void ChatModel::cleanupGifAnimations(const QList<QUrl> &visibleUrls)
{
    QSet<QUrl> visible(visibleUrls.begin(), visibleUrls.end());

    for (auto it = gifAttachmentAnimations.begin(); it != gifAttachmentAnimations.end();) {
        if (!visible.contains(it.key())) {
            auto *anim = it.value();
            // Disconnect all signals from anim BEFORE releasing it, so any
            // queued frameChanged signal can't dereference a dangling pointer
            // between erase and the actual deleteLater destruction.
            anim->pause();
            disconnect(anim, nullptr, this, nullptr);
            imageManager->releaseGifAnimation(it.key());
            gifAnimationRows.remove(it.key());
            it = gifAttachmentAnimations.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace UI
} // namespace Acheron

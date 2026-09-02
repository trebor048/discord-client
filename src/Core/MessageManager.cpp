#include "MessageManager.hpp"

#include <QDateTime>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>

#include <algorithm>

#include "Discord/Client.hpp"
#include "Markdown/Parser.hpp"
#include "Logging.hpp"
#include "UserManager.hpp"

namespace Acheron {
namespace Core {

static QString resolveUserJoinMessage(const Discord::Message &msg)
{
    QString author = msg.author->getDisplayName();
    qint64 ms = msg.timestamp->toMSecsSinceEpoch();
    switch (ms % 13) {
    case 0:
        return author + QStringLiteral(" joined the party.");
    case 1:
        return author + QStringLiteral(" is here.");
    case 2:
        return QStringLiteral("Welcome, ") + author + QStringLiteral(". We hope you brought pizza.");
    case 3:
        return QStringLiteral("A wild ") + author + QStringLiteral(" appeared.");
    case 4:
        return author + QStringLiteral(" just landed.");
    case 5:
        return author + QStringLiteral(" just slid into the server.");
    case 6:
        return author + QStringLiteral(" just showed up!");
    case 7:
        return QStringLiteral("Welcome ") + author + QStringLiteral(". Say hi!");
    case 8:
        return author + QStringLiteral(" hopped into the server.");
    case 9:
        return QStringLiteral("Everyone welcome ") + author + QStringLiteral("!");
    case 10:
        return QStringLiteral("Glad you're here, ") + author + QStringLiteral(".");
    case 11:
        return QStringLiteral("Good to see you, ") + author + QStringLiteral(".");
    case 12:
        return QStringLiteral("Yay you made it, ") + author + QStringLiteral("!");
    default:
        return author + QStringLiteral(" joined the party.");
    }
}

static QString resolveSystemMessageContent(const Discord::Message &msg)
{
    QString author = msg.author->getDisplayName();
    switch (static_cast<Discord::MessageType>(msg.type.get())) {
    case Discord::MessageType::CALL:
        return author + QStringLiteral(" started a call.");
    case Discord::MessageType::USER_JOIN:
        return resolveUserJoinMessage(msg);
    default:
        return msg.content;
    }
}

MessageManager::MessageManager(Snowflake accountId, Discord::Client *client,
                               UserManager *userManager, QObject *parent)
    : QObject(parent), client(client), userManager(userManager), repo(accountId), parser(std::make_unique<Markdown::Parser>())
{
    messageCache.setMaxCost(1'000);

    parser->setUserResolver([this](const QString &userId) {
        Snowflake id(userId.toULongLong());
        return this->userManager->getDisplayName(id);
    });
}

MessageManager::~MessageManager() {}

void MessageManager::parseMessageContent(Discord::Message &msg)
{
    Markdown::ParseState state;
    state.isInline = true;
    auto ast = parser->parse(resolveSystemMessageContent(msg), state);
    bool jumbo = Markdown::Parser::isEmojiOnly(ast);
    msg.parsedContentCached = parser->toHtml(ast, jumbo);

    if (msg.type.hasValue() && msg.type.get() == Discord::MessageType::THREAD_STARTER_MESSAGE &&
        msg.referencedMessage && msg.referencedMessage->content.hasValue() &&
        msg.referencedMessage->parsedContentCached.isEmpty()) {
        Markdown::ParseState refState;
        refState.isInline = true;
        auto refAst = parser->parse(msg.referencedMessage->content.get(), refState);
        msg.referencedMessage->parsedContentCached = parser->toHtml(refAst, Markdown::Parser::isEmojiOnly(refAst));
    }
}

void MessageManager::setChannelResolver(std::function<QString(Snowflake)> resolver)
{
    parser->setChannelResolver([resolver](const QString &channelId) {
        Snowflake id(channelId.toULongLong());
        return resolver(id);
    });
}

void MessageManager::requestLoadChannel(Snowflake channelId)
{
    if (fetchedChannels.contains(channelId)) {
        // ram cache — return the FULL cached history so switching back to a
        // channel preserves everything the user has already loaded, instead of
        // truncating to the latest 30 messages and losing scroll/history.
        if (channelMessages.contains(channelId)) {
            const auto &order = channelMessages[channelId];

            bool cached = true;
            QList<Discord::Message> result;
            result.reserve(order.size());

            for (Snowflake msgId : order) {
                if (auto *msg = messageCache.object(msgId)) {
                    result.append(*msg);
                } else {
                    cached = false;
                    break;
                }
            }

            if (cached) {
                emit messagesReceived({
                        true,
                        Discord::Client::MessageLoadType::Latest,
                        channelId,
                        result,
                });
                return;
            }
        }

        // disk cache
        QList<Discord::Message> msgs = repo.getLatestMessages(channelId, 30);
        if (!msgs.isEmpty()) { // probably good
            // Reuse the persisted rendered markdown; only fall back to parsing
            // for rows written before parsed_content existed (NULL column).
            for (auto &msg : msgs)
                if (msg.parsedContentCached.isEmpty())
                    parseMessageContent(msg);

            emit messagesReceived({
                    true,
                    Discord::Client::MessageLoadType::Latest,
                    channelId,
                    msgs,
            });
            return;
        }
    }

    QPointer<MessageManager> guard = this;
    client->fetchLatestMessages(
            channelId, 30, [this, guard, channelId](const Result<QList<Discord::Message>> &result) {
                if (!guard)
                    return;

                if (!result.success()) {
                    qWarning() << "Failed to fetch messages" << result.error;
                } else {
                    onApiMessagesReceived(result.value.value(),
                                          Discord::Client::MessageLoadType::Latest, channelId);
                }
            });
}

void MessageManager::requestMessage(Snowflake channelId, Snowflake messageId)
{
    QPointer<MessageManager> guard = this;
    client->fetchMessage(channelId, messageId,
                         [this, guard, channelId, messageId](const Result<QList<Discord::Message>> &result) {
                             if (!guard)
                                 return;

                             if (!result.success() || result.value.value().isEmpty()) {
                                 emit messageJumpFailed(channelId, messageId);
                                 return;
                             }

                             onApiMessagesReceived(result.value.value(),
                                                   Discord::Client::MessageLoadType::Jump,
                                                   channelId);
                             emit messageJumpReady(channelId, messageId);
                         });
}

void MessageManager::requestLoadHistory(Snowflake channelId, Snowflake beforeId)
{
    if (historyDebounce.contains(channelId))
        return;

    if (lowestKnownId.contains(channelId)) {
        // nothing to see here
        const auto &known = lowestKnownId[channelId];
        if (!known.has_value() || *known >= beforeId) {
            emit messagesReceived({
                    true,
                    Discord::Client::MessageLoadType::History,
                    channelId,
                    {},
            });
            return;
        }
    }

    if (fetchedChannels.contains(channelId)) {
        // ram cache
        const auto &order = channelMessages[channelId];
        if (!order.empty()) {
            auto it = std::lower_bound(order.begin(), order.end(), beforeId);
            int index = std::distance(order.begin(), it);

            if (index > 0) {
                int count = std::min(index, 30);
                int startIndex = index - count;

                bool cached = true;
                QList<Discord::Message> result;
                result.reserve(count);

                for (int i = startIndex; i < index; i++) {
                    Snowflake msgId = order[i];
                    if (auto *msg = messageCache.object(msgId)) {
                        result.append(*msg);
                    } else {
                        cached = false;
                        break;
                    }
                }

                if (cached) {
                    emit messagesReceived({
                            true,
                            Discord::Client::MessageLoadType::History,
                            channelId,
                            result,
                    });
                    return;
                }
            }
        }

        // disk cache
        QList<Discord::Message> msgs = repo.getMessagesBefore(channelId, beforeId, 30);

        // Reuse the persisted rendered markdown; only fall back to parsing for
        // rows written before parsed_content existed (NULL column).
        for (auto &msg : msgs)
            if (msg.parsedContentCached.isEmpty())
                parseMessageContent(msg);

        if (!msgs.isEmpty()) { // probably good
            emit messagesReceived({
                    true,
                    Discord::Client::MessageLoadType::History,
                    channelId,
                    msgs,
            });
            return;
        }
    }

    historyDebounce.insert(channelId);

    QPointer<MessageManager> guard = this;
    client->fetchHistory(channelId, beforeId, 30,
                         [this, guard, channelId](const Result<QList<Discord::Message>> &result) {
                             if (!guard)
                                 return;

                             historyDebounce.remove(channelId);

                             if (!result.success()) {
                                 qWarning() << "Failed to fetch history" << result.error;
                                 emit messagesReceived({
                                         false,
                                         Discord::Client::MessageLoadType::History,
                                 });
                             } else {
                                 onApiMessagesReceived(result.value.value(),
                                                       Discord::Client::MessageLoadType::History,
                                                       channelId);
                             }
                         });
}

void MessageManager::cacheMessages(Snowflake channelId, const QList<Discord::Message> &msgs)
{
    if (msgs.isEmpty())
        return;

    auto &order = channelMessages[channelId];

    // Cache message objects (the QCache owns the heap copies).
    for (const auto &msg : msgs)
        messageCache.insert(msg.id, new Discord::Message(msg));

    if (order.empty()) {
        for (const auto &msg : msgs)
            order.push_back(msg.id);
    } else if (msgs.first().id > order.back()) {
        // Common fast path: incoming batch is entirely newer than what we have
        // (Latest / new message batches). Append in O(n). Strict comparison:
        // a batch starting with an id we already hold (e.g. a single-message
        // jump fetch of a loaded message) must fall through to the merge so it
        // is deduplicated instead of appended twice.
        for (const auto &msg : msgs)
            order.push_back(msg.id);
    } else if (msgs.last().id < order.front()) {
        // History batches are entirely older; prepend in O(n).
        for (auto it = msgs.crbegin(); it != msgs.crend(); ++it)
            order.push_front(it->id);
    } else {
        // General merge of two sorted ranges. This is rare in practice.
        std::deque<Snowflake> merged;
        auto oit = order.begin();
        auto mit = msgs.begin();
        while (oit != order.end() && mit != msgs.end()) {
            if (*oit < mit->id) {
                merged.push_back(*oit++);
            } else if (*oit > mit->id) {
                merged.push_back(mit->id);
                ++mit;
            } else {
                merged.push_back(*oit++);
                ++mit; // duplicate
            }
        }
        while (oit != order.end())
            merged.push_back(*oit++);
        while (mit != msgs.end())
            merged.push_back(mit->id), ++mit;
        order = std::move(merged);
    }

    // bound per-channel ID lists — the message cache is capped at 1000 entries
    // anyway, so retaining unbounded ID lists only wastes memory
    constexpr qsizetype MaxChannelMessageIds = 2'000;
    while (order.size() > MaxChannelMessageIds)
        order.pop_front();
}

void MessageManager::onMessageCreated(const Discord::Message &message)
{
    // Remove any preview (outbound) message with matching nonce to prevent
    // duplicate entries — the preview used a local-only Snowflake ID, but
    // the real message from Discord now carries the server-assigned one.
    if (message.nonce.hasValue() && !message.nonce->isEmpty()) {
        auto it = pendingSends.find(message.nonce.get());
        if (it != pendingSends.end()) {
            Snowflake cachedId = it->messageId;
            Snowflake channelId = it->channelId;
            const QString nonce = it.key();
            pendingSends.erase(it);

            emit messageSendSucceeded(nonce);

            messageCache.remove(cachedId);
            if (channelMessages.contains(channelId)) {
                auto &order = channelMessages[channelId];
                order.erase(std::remove(order.begin(), order.end(), cachedId), order.end());
            }
        }
    }
    onApiMessagesReceived({ message }, Discord::Client::MessageLoadType::Created,
                          message.channelId);
}

void MessageManager::onMessageUpdated(const Discord::Message &message)
{
    Discord::Message merged;
    bool haveBaseline = false;

    if (auto *cached = messageCache.object(message.id)) {
        merged = *cached;
        haveBaseline = true;
    } else if (auto existing = repo.getMessage(message.id)) {
        merged = *existing;
        haveBaseline = true;
    }

    // Content/type drive the markdown parse; capture the persisted fields
    // before applying so a reaction/embed/flag-only update skips both the
    // re-parse and the full-row UPDATE (reactions are persisted separately).
    const QString oldContent = haveBaseline ? merged.content.get() : QString();
    const qint64 oldType = haveBaseline ? static_cast<qint64>(merged.type.get()) : -1;
    const QDateTime oldEdited = haveBaseline ? merged.editedTimestamp.get() : QDateTime();
    const QString oldEmbeds = haveBaseline ? merged.embedsJson : QString();
    const qint64 oldFlags = haveBaseline ? static_cast<qint64>(merged.flags.get()) : 0;

    if (haveBaseline) {
        merged.applyUpdate(message);
    } else {
        if (!message.presentKeys.contains(QStringLiteral("content"))) {
            return;
        }
        merged = message;
    }

    // Most MESSAGE_UPDATEs carry only reactions/embeds/flags. Re-parsing the
    // (unchanged) content each time wasted a full markdown pass on the UI
    // thread; the parsed HTML only needs rebuilding when content or type
    // actually changed, or when no cached parse exists yet (disk reload).
    const bool contentChanged = merged.content != oldContent
                                || static_cast<qint64>(merged.type.get()) != oldType;
    if (contentChanged || merged.parsedContentCached.isEmpty())
        parseMessageContent(merged);

    if (messageCache.contains(message.id))
        messageCache.insert(message.id, new Discord::Message(merged));

    // Persist only when a persisted column actually changed. Reaction-only
    // updates are handled by the targeted updateReactionsJson below instead
    // of a full-row UPDATE on the UI thread.
    const bool metaChanged = merged.editedTimestamp.get() != oldEdited
                             || merged.embedsJson != oldEmbeds
                             || static_cast<qint64>(merged.flags.get()) != oldFlags;
    if (contentChanged || metaChanged)
        repo.updateMessageContent(merged);

    if (message.presentKeys.contains(QStringLiteral("reactions")))
        repo.updateReactionsJson(merged.id, merged.reactionsJson);

    emit messagesReceived({ true, Discord::Client::MessageLoadType::Updated, message.channelId, { merged } });
}

void MessageManager::onMessageDeleted(const Discord::MessageDelete &event)
{
    Snowflake channelId = event.channelId.get();
    Snowflake messageId = event.id.get();

    messageCache.remove(messageId);

    if (channelMessages.contains(channelId)) {
        auto &order = channelMessages[channelId];
        auto it = std::find(order.begin(), order.end(), messageId);
        if (it != order.end())
            order.erase(it);
    }

    repo.markMessageDeleted(messageId);

    emit messageDeleted(channelId, messageId);
}

void MessageManager::onMessagesDeletedBulk(const Discord::MessageDeleteBulk &event)
{
    if (!event.channelId.hasValue() || !event.ids.hasValue())
        return;

    Snowflake channelId = event.channelId.get();
    const QList<Snowflake> ids = event.ids.get();

    // Prune the per-channel order list in ONE pass instead of a std::find +
    // erase per id (O(n·k) -> O(n + k)). erase/remove_if preserves the
    // relative order of the survivors, exactly like erasing each id in turn.
    const QSet<Snowflake> deletedIds(ids.cbegin(), ids.cend());
    if (channelMessages.contains(channelId)) {
        auto &order = channelMessages[channelId];
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&deletedIds](Snowflake id) { return deletedIds.contains(id); }),
                    order.end());
    }

    // Per-id cache removal and emits are part of the UI contract (each message
    // row reacts independently); only the repository write is batched.
    for (Snowflake messageId : ids) {
        messageCache.remove(messageId);
        emit messageDeleted(channelId, messageId);
    }

    repo.markMessagesDeleted(ids);
}

void MessageManager::onMessageSendFailed(const QString &nonce, const QString &error)
{
    qCWarning(LogCore) << "Message send failed for nonce" << nonce << ":" << error;

    auto it = pendingSends.find(nonce);
    if (it == pendingSends.end()) {
        emit messageErrored(nonce);
        return;
    }

    // Remove the pending preview entirely — a failed send must not linger in
    // the UI as a ghost message.
    Snowflake channelId = it->channelId;
    Snowflake messageId = it->messageId;
    pendingSends.erase(it);

    messageCache.remove(messageId);
    if (channelMessages.contains(channelId)) {
        auto &order = channelMessages[channelId];
        auto oit = std::find(order.begin(), order.end(), messageId);
        if (oit != order.end())
            order.erase(oit);
    }

    emit messageDeleted(channelId, messageId);
    emit messageErrored(nonce);
}

QString MessageManager::sendMessage(Snowflake channelId, const QString &content,
                                    Snowflake replyToMessageId,
                                    const QList<PendingAttachment> &attachments)
{
    Snowflake nonceId = Snowflake::generateNonce();
    QString nonce = QString::number(nonceId);

    auto outgoing = attachments;

    Discord::Message preview;
    preview.id = nonceId; // temporary id, will be overwritten
    preview.nonce = nonce;
    preview.channelId = channelId;
    preview.content = content;
    preview.timestamp = QDateTime::currentDateTimeUtc();
    preview.author = client->getMe();
    preview.flags = Discord::MessageFlags(0);
    preview.isPendingOutbound = true;

    if (!outgoing.isEmpty()) {
        QList<Discord::Attachment> previewAttachments;
        for (int i = 0; i < outgoing.size(); i++) {
            const auto &att = outgoing[i];
            Discord::Attachment a;
            a.id = Snowflake(nonceId + i + 1);
            a.filename = att.filename;
            a.size = att.size;
            a.contentType = att.mimeType;
            if (!att.image.isNull()) {
                a.localPreview = att.image;
                a.width = att.image.width();
                a.height = att.image.height();
            } else if (att.mimeType.startsWith("image/") && !att.filePath.isEmpty()) {
                QString localUrl = QUrl::fromLocalFile(att.filePath).toString();
                a.url = localUrl;
                a.proxyUrl = localUrl;
                QSize dims = QImageReader(att.filePath).size();
                if (dims.isValid()) {
                    a.width = dims.width();
                    a.height = dims.height();
                }
            }
            if (att.isSpoiler)
                a.flags = Discord::AttachmentFlags(Discord::AttachmentFlag::IS_SPOILER);
            previewAttachments.append(a);
        }
        preview.attachments = previewAttachments;
    }

    // Validate attachments BEFORE emitting the preview — otherwise a failed
    // attachment shows a ghost message in the UI.
    for (const auto &att : outgoing) {
        if (att.data.isEmpty() && att.filePath.isEmpty()) {
            emit messageErrored(nonce);
            return nonce;
        }
    }

    if (replyToMessageId.isValid()) {
        preview.type = Discord::MessageType::REPLY;
        Discord::MessageReference ref;
        ref.messageId = replyToMessageId;
        ref.channelId = channelId;
        preview.messageReference = ref;
    } else {
        preview.type = Discord::MessageType::DEFAULT;
    }

    Markdown::ParseState state;
    state.isInline = true;
    auto ast = parser->parse(content, state);
    bool jumbo = Markdown::Parser::isEmojiOnly(ast);
    preview.parsedContentCached = parser->toHtml(ast, jumbo);

    pendingSends.insert(nonce, { channelId, nonceId });

    // Notify listeners before the synchronous Created batch is emitted so
    // they can register this nonce as a known local pending send.
    emit messageSendPending(nonce);

    // get our fake preview in
    emit messagesReceived(
            { true, Discord::Client::MessageLoadType::Created, channelId, { preview } });

    client->sendMessage(channelId, content, nonce, replyToMessageId, outgoing);
    return nonce;
}

void MessageManager::cancelSend(Snowflake channelId, const QString &nonce)
{
    if (!client->cancelMessageSend(nonce))
        return;

    pendingSends.remove(nonce);

    Snowflake nonceId(nonce.toULongLong());
    messageCache.remove(nonceId);
    if (channelMessages.contains(channelId)) {
        auto &order = channelMessages[channelId];
        auto it = std::find(order.begin(), order.end(), nonceId);
        if (it != order.end())
            order.erase(it);
    }
    emit messageDeleted(channelId, nonceId);
}

static bool emojisMatch(const Discord::Emoji &a, const Discord::Emoji &b)
{
    if (!a.isUnicode() && !b.isUnicode())
        return a.id.get() == b.id.get();
    if (a.isUnicode() && b.isUnicode())
        return a.name.get() == b.name.get();
    return false;
}

static QString reactionsToJson(const QList<Discord::Reaction> &reactions)
{
    if (reactions.isEmpty())
        return {};

    QJsonArray arr;
    for (const auto &r : reactions) {
        QJsonObject emojiObj;
        if (!r.emoji->isUnicode())
            emojiObj["id"] = r.emoji->id->toString();
        else
            emojiObj["id"] = QJsonValue::Null;
        emojiObj["name"] = *r.emoji->name;
        if (r.emoji->animated.hasValue())
            emojiObj["animated"] = *r.emoji->animated;

        QJsonObject obj;
        obj["emoji"] = emojiObj;
        obj["count"] = *r.count;
        obj["me"] = *r.me;

        if (r.countDetails.hasValue()) {
            QJsonObject details;
            details["burst"] = *r.countDetails->burst;
            details["normal"] = *r.countDetails->normal;
            obj["count_details"] = details;
        }

        if (r.meBurst.hasValue())
            obj["me_burst"] = *r.meBurst;
        if (r.burstCount.hasValue())
            obj["burst_count"] = *r.burstCount;

        if (r.burstColors.hasValue()) {
            QJsonArray colors;
            for (const auto &c : *r.burstColors)
                colors.append(c);
            obj["burst_colors"] = colors;
        }

        arr.append(obj);
    }

    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

static void rebuildReactionsJson(Discord::Message &msg)
{
    if (!msg.reactions.hasValue()) {
        msg.reactionsJson.clear();
        return;
    }
    msg.reactionsJson = reactionsToJson(*msg.reactions);
}

static QList<Discord::Reaction> reactionsFromJson(const QString &json)
{
    QList<Discord::Reaction> reactions;
    if (json.isEmpty())
        return reactions;

    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isArray()) {
        for (const QJsonValue &val : doc.array())
            reactions.append(Discord::Reaction::fromJson(val.toObject()));
    }
    return reactions;
}

void MessageManager::emitReactionUpdate(Discord::Message &msg)
{
    rebuildReactionsJson(msg);
    messageCache.insert(msg.id, new Discord::Message(msg));
    // Reactions live in a single column; a targeted UPDATE is far cheaper than
    // the full INSERT + user + attachments + prune + commit transaction that
    // saveMessages runs (which happened here on every reaction click on the UI
    // thread). Cached messages are always persisted, so the row exists.
    repo.updateReactionsJson(msg.id, msg.reactionsJson);
    emit messagesReceived({ true, Discord::Client::MessageLoadType::Updated, msg.channelId, { msg } });
}

static void applyReactionAdd(QList<Discord::Reaction> &reactions,
                             const Discord::Emoji &emoji, bool isBurst, bool isMe,
                             const QList<QString> &burstColors = {})
{
    bool found = false;
    for (auto &r : reactions) {
        if (emojisMatch(r.emoji, emoji)) {
            r.count = *r.count + 1;
            if (r.countDetails.hasValue()) {
                if (isBurst)
                    r.countDetails->burst = *r.countDetails->burst + 1;
                else
                    r.countDetails->normal = *r.countDetails->normal + 1;
            }
            // Keep the persisted burst_count in sync with countDetails->burst;
            // both are stored in the reactions JSON and would otherwise diverge
            // (the serialized cache would carry a stale burst total after a
            // live burst add on an existing reaction).
            if (isBurst && r.burstCount.hasValue())
                r.burstCount = *r.burstCount + 1;
            if (isMe) {
                if (isBurst)
                    r.meBurst = true;
                else
                    r.me = true;
            }
            if (isBurst && !burstColors.isEmpty())
                r.burstColors = burstColors;
            found = true;
            break;
        }
    }

    if (!found) {
        Discord::Reaction newReaction;
        newReaction.emoji = emoji;
        newReaction.count = 1;
        newReaction.me = !isBurst && isMe;
        newReaction.meBurst = isBurst && isMe;
        newReaction.burstCount = isBurst ? 1 : 0;

        Discord::ReactionCountDetails details;
        details.burst = isBurst ? 1 : 0;
        details.normal = isBurst ? 0 : 1;
        newReaction.countDetails = details;

        if (isBurst && !burstColors.isEmpty())
            newReaction.burstColors = burstColors;

        reactions.append(newReaction);
    }
}

void MessageManager::onReactionAdd(const Discord::MessageReactionAdd &event)
{
    bool isBurst = event.type.hasValue() && *event.type == 1;
    bool isMe = event.userId.get() == client->getMe().id.get();

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        QList<QString> colors = event.burstColors.hasValue() ? *event.burstColors : QList<QString>{};
        applyReactionAdd(reactions, event.emoji, isBurst, isMe, colors);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        msg.reactions = QList<Discord::Reaction>();

    QList<QString> colors = event.burstColors.hasValue() ? *event.burstColors : QList<QString>{};
    applyReactionAdd(*msg.reactions, event.emoji, isBurst, isMe, colors);

    emitReactionUpdate(msg);
}

static void applyReactionAddMany(QList<Discord::Reaction> &reactions,
                                 const Discord::MessageReactionAddMany &event, Snowflake myId)
{
    for (const auto &debounced : *event.reactions) {
        bool isMe = false;
        for (const auto &uid : *debounced.users) {
            if (uid == myId) {
                isMe = true;
                break;
            }
        }

        int addCount = debounced.users->size();
        bool found = false;
        for (auto &r : reactions) {
            if (emojisMatch(r.emoji, debounced.emoji)) {
                r.count = *r.count + addCount;
                if (r.countDetails.hasValue())
                    r.countDetails->normal = *r.countDetails->normal + addCount;
                if (isMe)
                    r.me = true;
                found = true;
                break;
            }
        }

        if (!found) {
            Discord::Reaction newReaction;
            newReaction.emoji = debounced.emoji;
            newReaction.count = addCount;
            newReaction.me = isMe;
            newReaction.meBurst = false;
            newReaction.burstCount = 0;

            Discord::ReactionCountDetails details;
            details.burst = 0;
            details.normal = addCount;
            newReaction.countDetails = details;

            reactions.append(newReaction);
        }
    }
}

void MessageManager::onReactionAddMany(const Discord::MessageReactionAddMany &event)
{
    Snowflake myId = client->getMe().id;

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        applyReactionAddMany(reactions, event, myId);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        msg.reactions = QList<Discord::Reaction>();

    applyReactionAddMany(*msg.reactions, event, myId);

    emitReactionUpdate(msg);
}

static void applyReactionRemove(QList<Discord::Reaction> &reactions,
                                const Discord::Emoji &emoji, bool isBurst, bool isMe)
{
    for (int i = 0; i < reactions.size(); ++i) {
        auto &r = reactions[i];
        if (emojisMatch(r.emoji, emoji)) {
            r.count = *r.count - 1;
            if (r.countDetails.hasValue()) {
                if (isBurst)
                    r.countDetails->burst = qMax(0, *r.countDetails->burst - 1);
                else
                    r.countDetails->normal = qMax(0, *r.countDetails->normal - 1);
            }
            // Mirror the decrement in the persisted burst_count (see
            // applyReactionAdd).
            if (isBurst && r.burstCount.hasValue())
                r.burstCount = qMax(0, *r.burstCount - 1);
            if (isMe) {
                if (isBurst)
                    r.meBurst = false;
                else
                    r.me = false;
            }

            if (*r.count <= 0)
                reactions.removeAt(i);

            break;
        }
    }
}

void MessageManager::onReactionRemove(const Discord::MessageReactionRemove &event)
{
    bool isBurst = event.type.hasValue() && *event.type == 1;
    bool isMe = event.userId.get() == client->getMe().id.get();

    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        applyReactionRemove(reactions, event.emoji, isBurst, isMe);
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        return;

    applyReactionRemove(*msg.reactions, event.emoji, isBurst, isMe);

    emitReactionUpdate(msg);
}

void MessageManager::onReactionRemoveAll(const Discord::MessageReactionRemoveAll &event)
{
    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        repo.updateReactionsJson(event.messageId, {});
        return;
    }

    Discord::Message msg = *cached;
    msg.reactions = QList<Discord::Reaction>();

    emitReactionUpdate(msg);
}

void MessageManager::onReactionRemoveEmoji(const Discord::MessageReactionRemoveEmoji &event)
{
    auto *cached = messageCache.object(event.messageId);
    if (!cached) {
        QList<Discord::Reaction> reactions =
                reactionsFromJson(repo.getReactionsJson(event.messageId));
        for (int i = 0; i < reactions.size(); ++i) {
            if (emojisMatch(reactions[i].emoji, event.emoji)) {
                reactions.removeAt(i);
                break;
            }
        }
        repo.updateReactionsJson(event.messageId, reactionsToJson(reactions));
        return;
    }

    Discord::Message msg = *cached;

    if (!msg.reactions.hasValue())
        return;

    for (int i = 0; i < msg.reactions->size(); ++i) {
        if (emojisMatch((*msg.reactions)[i].emoji, event.emoji)) {
            msg.reactions->removeAt(i);
            break;
        }
    }

    emitReactionUpdate(msg);
}

void MessageManager::onApiMessagesReceived(const QList<Discord::Message> &messages,
                                           Discord::Client::MessageLoadType type,
                                           Snowflake channelId)
{
    auto sortedMessages = messages;
    std::sort(sortedMessages.begin(), sortedMessages.end(),
              [](const auto &a, const auto &b) { return a.id.get() < b.id.get(); });

    if (type == Discord::Client::MessageLoadType::History) {
        // hit the end probably
        // maybe 0 could happen mistakenly prob not tho
        // only history loads prove exhaustion — a partial Latest fetch says
        // nothing about how far back the channel's history goes
        if (sortedMessages.isEmpty())
            lowestKnownId[channelId] = std::nullopt;
        else if (sortedMessages.size() < 30)
            lowestKnownId[channelId] = sortedMessages.first().id;
    }

    // Parse BEFORE persisting so the rendered markdown is stored in the cache
    // DB and the next disk reload reuses it instead of re-parsing every
    // message on the UI thread.
    for (auto &msg : sortedMessages)
        parseMessageContent(msg);

    repo.saveMessages(sortedMessages);

    for (const auto &msg : sortedMessages) {
        // cache owns its own copy
        Discord::Message *toCache = new Discord::Message(msg);
        messageCache.insert(toCache->id, toCache); // should remove old copies
    }

    if (type == Discord::Client::MessageLoadType::Latest) {
        fetchedChannels.insert(channelId);
        // Drop the stale tail covered by this fresh fetch, but retain older
        // history IDs below the fetched range so loaded history survives.
        if (!sortedMessages.isEmpty()) {
            auto &order = channelMessages[channelId];
            auto it = std::lower_bound(order.begin(), order.end(), sortedMessages.first().id);
            order.erase(it, order.end());
        }
    }

    cacheMessages(channelId, sortedMessages);

    emit messagesReceived({ true, type, channelId, sortedMessages });
}

} // namespace Core
} // namespace Acheron

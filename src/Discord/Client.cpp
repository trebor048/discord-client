#include "Client.hpp"

#include <QDebug>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#include "Enums.hpp"
#include "Core/Logging.hpp"
#include "Proto/ProtoReader.hpp"
#include "Proto/UserSettings.hpp"

namespace Acheron {
namespace Discord {

using Core::Snowflake;

namespace {

Proto::GuildFolders guildFoldersFromLegacy(const QList<GuildFolderEntry> &entries)
{
    Proto::GuildFolders result;
    result.folders.reserve(entries.size());
    for (const auto &entry : entries) {
        Proto::GuildFolder folder;
        folder.guildIds = entry.guildIds.get();
        if (entry.id.hasValue())
            folder.id = entry.id.get();
        if (entry.name.hasValue())
            folder.name = entry.name.get();
        if (entry.color.hasValue())
            folder.color = static_cast<uint64_t>(entry.color.get());
        result.folders.append(folder);
    }
    return result;
}

} // namespace

Client::Client(const QString &token, const QString &gatewayUrl, const QString &baseUrl,
               CaptchaResolver *captchaResolver, QObject *parent)
    : QObject(parent), token(token), baseUrl(baseUrl)
{
    identity.regenerateClientHeartbeatSessionId();

    gateway = new Gateway(token, gatewayUrl, identity, this);
    httpClient = new HttpClient(baseUrl, token, identity, captchaResolver, this);

    restorePendingUnbans();

    connect(gateway, &Gateway::connected, this, &Client::onConnected);
    connect(gateway, &Gateway::disconnected, this, &Client::onDisconnected);

    connect(gateway, &Gateway::gatewayHello, this, [] {
        qCInfo(LogDiscord) << "Gateway hello received, handshaking";
    });
    connect(gateway, &Gateway::gatewayReady, this, &Client::onGatewayReady);
    connect(gateway, &Gateway::gatewayReadySupplemental, this, &Client::onGatewayReadySupplemental);
    connect(gateway, &Gateway::gatewayMessageCreate, this, &Client::onGatewayMessageCreate);
    connect(gateway, &Gateway::gatewayMessageUpdate, this, &Client::onGatewayMessageUpdate);
    connect(gateway, &Gateway::gatewayMessageDelete, this, &Client::onGatewayMessageDelete);
    connect(gateway, &Gateway::gatewayMessageDeleteBulk, this, &Client::messagesDeletedBulk);
    connect(gateway, &Gateway::gatewayTypingStart, this, &Client::typingStart);
    connect(gateway, &Gateway::gatewayChannelCreate, this, &Client::onGatewayChannelCreate);
    connect(gateway, &Gateway::gatewayChannelUpdate, this, &Client::onGatewayChannelUpdate);
    connect(gateway, &Gateway::gatewayChannelDelete, this, &Client::onGatewayChannelDelete);
    connect(gateway, &Gateway::gatewayThreadCreate, this, &Client::onGatewayThreadCreate);
    connect(gateway, &Gateway::gatewayThreadUpdate, this, &Client::onGatewayThreadUpdate);
    connect(gateway, &Gateway::gatewayThreadDelete, this, &Client::onGatewayThreadDelete);
    connect(gateway, &Gateway::gatewayThreadListSync, this, &Client::onGatewayThreadListSync);
    connect(gateway, &Gateway::gatewayThreadMemberUpdate, this, &Client::threadMemberUpdated);
    connect(gateway, &Gateway::gatewayThreadMembersUpdate, this, &Client::threadMembersUpdated);
    connect(gateway, &Gateway::gatewayForumUnreads, this, &Client::forumUnreads);
    connect(gateway, &Gateway::gatewayGuildCreate, this, &Client::onGatewayGuildCreate);
    connect(gateway, &Gateway::gatewayGuildDelete, this, &Client::onGatewayGuildDelete);
    connect(gateway, &Gateway::gatewayGuildMembersChunk, this, &Client::guildMembersChunk);
    connect(gateway, &Gateway::gatewayGuildMemberUpdate, this, &Client::guildMemberUpdated);
    connect(gateway, &Gateway::gatewayGuildMemberAdd, this, &Client::guildMemberAdded);
    connect(gateway, &Gateway::gatewayGuildMemberRemove, this, &Client::guildMemberRemoved);
    connect(gateway, &Gateway::gatewayUserUpdate, this, &Client::onGatewayUserUpdate);
    connect(gateway, &Gateway::gatewayGuildRoleCreate, this, &Client::onGatewayGuildRoleCreate);
    connect(gateway, &Gateway::gatewayGuildRoleUpdate, this, &Client::onGatewayGuildRoleUpdate);
    connect(gateway, &Gateway::gatewayGuildRoleDelete, this, &Client::onGatewayGuildRoleDelete);
    connect(gateway, &Gateway::gatewayMessageAck, this, &Client::messageAcked);
    connect(gateway, &Gateway::gatewayMessageReactionAdd, this, &Client::messageReactionAdd);
    connect(gateway, &Gateway::gatewayMessageReactionAddMany, this, &Client::messageReactionAddMany);
    connect(gateway, &Gateway::gatewayMessageReactionRemove, this, &Client::messageReactionRemove);
    connect(gateway, &Gateway::gatewayMessageReactionRemoveAll, this, &Client::messageReactionRemoveAll);
    connect(gateway, &Gateway::gatewayMessageReactionRemoveEmoji, this, &Client::messageReactionRemoveEmoji);
    connect(gateway, &Gateway::gatewayUserGuildSettingsUpdate, this,
            &Client::userGuildSettingsUpdated);
    connect(gateway, &Gateway::gatewayGuildMemberListUpdate, this, &Client::guildMemberListUpdate);
    connect(gateway, &Gateway::gatewayVoiceStateUpdate, this, &Client::voiceStateUpdated);
    connect(gateway, &Gateway::gatewayVoiceServerUpdate, this, &Client::voiceServerUpdated);
    connect(gateway, &Gateway::gatewayRelationshipAdd, this, &Client::relationshipAdded);
    connect(gateway, &Gateway::gatewayRelationshipUpdate, this, &Client::relationshipUpdated);
    connect(gateway, &Gateway::gatewayRelationshipRemove, this, &Client::relationshipRemoved);
    connect(gateway, &Gateway::gatewayUserNoteUpdate, this, &Client::userNoteUpdated);
    connect(gateway, &Gateway::gatewayGuildUpdate, this, &Client::guildUpdated);
    connect(gateway, &Gateway::gatewayGuildBanAdd, this, &Client::guildBanAdded);
    connect(gateway, &Gateway::gatewayGuildBanRemove, this, &Client::guildBanRemoved);
    connect(gateway, &Gateway::gatewayGuildEmojisUpdate, this, &Client::guildEmojisUpdated);
    connect(gateway, &Gateway::gatewayGuildStickersUpdate, this, &Client::guildStickersUpdated);
    connect(gateway, &Gateway::gatewayPresenceUpdate, this, &Client::presenceUpdated);
    connect(gateway, &Gateway::gatewayWebhooksUpdate, this, &Client::webhooksUpdated);
    connect(gateway, &Gateway::gatewayInviteCreate, this, &Client::inviteCreated);
    connect(gateway, &Gateway::gatewayInviteDelete, this, &Client::inviteDeleted);
    connect(gateway, &Gateway::gatewayStageInstanceCreate, this, &Client::stageInstanceCreated);
    connect(gateway, &Gateway::gatewayStageInstanceUpdate, this, &Client::stageInstanceUpdated);
    connect(gateway, &Gateway::gatewayStageInstanceDelete, this, &Client::stageInstanceDeleted);
    connect(gateway, &Gateway::gatewayScheduledEventCreate, this, &Client::scheduledEventCreated);
    connect(gateway, &Gateway::gatewayScheduledEventUpdate, this, &Client::scheduledEventUpdated);
    connect(gateway, &Gateway::gatewayScheduledEventDelete, this, &Client::scheduledEventDeleted);
    connect(gateway, &Gateway::gatewayIntegrationCreate, this, &Client::integrationCreated);
    connect(gateway, &Gateway::gatewayIntegrationUpdate, this, &Client::integrationUpdated);
    connect(gateway, &Gateway::gatewayIntegrationDelete, this, &Client::integrationDeleted);
    connect(gateway, &Gateway::gatewayChannelPinsUpdate, this, &Client::channelPinsUpdated);
    connect(gateway, &Gateway::reconnecting, this, [this](int attempt, int maxAttempts) {
        setState(Core::ConnectionState::Connecting);
        emit reconnecting(attempt, maxAttempts);
    });
}

Client::~Client()
{
    // join and delete the http clients thread before everything else
    delete httpClient;
    httpClient = nullptr;
}

void Client::start()
{
    setState(Core::ConnectionState::Connecting);
    gateway->start();
}

void Client::stop()
{
    setState(Core::ConnectionState::Disconnecting);
    gateway->stop();
}

[[nodiscard]] Core::ConnectionState Client::getState() const
{
    return state;
}

void Client::fetchLatestMessages(Snowflake channelId, int limit, MessagesCallback callback)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages";
    QUrlQuery query;
    query.addQueryItem("limit", QString::number(limit));

    httpClient->get(endpoint, query, [this, channelId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch latest messages: " << response.error;
            callback({ {}, "Failed to fetch latest messages: " + response.error });
            return;
        }

        QList<Message> results;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            results.append(Message::fromJson(val.toObject()));

        callback({ results });
    });
}

void Client::fetchHistory(Snowflake channelId, Snowflake beforeId, int limit,
                          MessagesCallback callback)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages";
    QUrlQuery query;
    query.addQueryItem("before", QString::number(beforeId));
    query.addQueryItem("limit", QString::number(limit));

    httpClient->get(endpoint, query, [this, channelId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch history: " << response.error;
            callback({ {}, "Failed to fetch history: " + response.error });
            return;
        }

        QList<Message> results;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            results.append(Message::fromJson(val.toObject()));

        callback({ results });
    });
}

void Client::fetchMessage(Snowflake channelId, Snowflake messageId, MessagesCallback callback)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId);

    httpClient->get(endpoint, QUrlQuery{}, [this, channelId, messageId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch message" << messageId << "in channel"
                                  << channelId << ":" << response.error;
            callback({ {}, "Failed to fetch message: " + response.error });
            return;
        }

        QList<Message> results;
        results.append(Message::fromJson(QJsonDocument::fromJson(response.body).object()));
        callback({ results });
    });
}

void Client::fetchUserProfile(Snowflake userId, Snowflake guildId, ProfileCallback callback)
{
    QString endpoint = "/users/" + QString::number(userId) + "/profile";
    QUrlQuery query;
    query.addQueryItem("type", "popout");
    query.addQueryItem("with_mutual_guilds", "true");
    query.addQueryItem("with_mutual_friends", "true");
    query.addQueryItem("with_mutual_friends_count", "false");
    if (guildId.isValid())
        query.addQueryItem("guild_id", QString::number(guildId));

    httpClient->get(endpoint, query, [userId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch user profile for" << userId << ":"
                                  << response.error;
            callback({ {}, "Failed to fetch user profile: " + response.error });
            return;
        }

        UserProfile profile = UserProfile::fromJson(QJsonDocument::fromJson(response.body).object());
        callback({ profile });
    });
}

void Client::fetchDMChannels(DMChannelsCallback callback)
{
    httpClient->get(QStringLiteral("/users/@me/channels"), {}, [this, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch DM channels: " << response.error;
            callback({ {}, "Failed to fetch DM channels: " + response.error });
            return;
        }

        QList<Channel> results;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            results.append(Channel::fromJson(val.toObject()));

        callback({ results });
    });
}

void Client::fetchApplicationCommands(Snowflake channelId, const QString &query, ApplicationCommandsCallback callback)
{
    QUrlQuery q;
    q.addQueryItem("type", "1"); // CHAT_INPUT
    q.addQueryItem("limit", "25");
    if (!query.isEmpty())
        q.addQueryItem("query", query);

    const QString endpoint =
            QStringLiteral("/channels/%1/application-commands/search").arg(channelId.toString());
    httpClient->get(endpoint, q, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch application commands:" << response.error;
            callback({ {}, "Failed to fetch application commands: " + response.error });
            return;
        }
        QList<ApplicationCommand> results;
        const QJsonDocument doc = QJsonDocument::fromJson(response.body);
        // The search endpoint returns { "application_commands": [...] }; accept a
        // bare array too for robustness.
        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else if (doc.isObject()) {
            arr = doc.object().value(QStringLiteral("application_commands")).toArray();
        }
        for (const QJsonValue &val : arr)
            results.append(ApplicationCommand::fromJson(val.toObject()));
        callback({ results });
    });
}

namespace {
QJsonObject applicationCommandOptionToJson(const ApplicationCommandOption &opt)
{
    QJsonObject o;
    o.insert("type", static_cast<int>(opt.type.get()));
    o.insert("name", opt.name.get());
    if (opt.description.hasValue())
        o.insert("description", opt.description.get());
    if (opt.required.hasValue())
        o.insert("required", opt.required.get());
    if (opt.choices.hasValue() && !opt.choices->isEmpty()) {
        QJsonArray arr;
        for (const auto &c : *opt.choices) {
            QJsonObject co;
            co.insert("name", c.name.get());
            co.insert("value", c.value.get());
            arr.append(co);
        }
        o.insert("choices", arr);
    }
    if (opt.options.hasValue() && !opt.options->isEmpty()) {
        QJsonArray arr;
        for (const auto &sub : *opt.options)
            arr.append(applicationCommandOptionToJson(sub));
        o.insert("options", arr);
    }
    return o;
}

QJsonObject applicationCommandToJson(const ApplicationCommand &cmd)
{
    QJsonObject o;
    o.insert("id", cmd.id.get().toString());
    o.insert("type", static_cast<int>(cmd.type.get()));
    o.insert("application_id", cmd.applicationId.get().toString());
    if (cmd.guildId.hasValue())
        o.insert("guild_id", cmd.guildId->toString());
    o.insert("name", cmd.name.get());
    if (cmd.description.hasValue())
        o.insert("description", cmd.description.get());
    if (cmd.options.hasValue() && !cmd.options->isEmpty()) {
        QJsonArray arr;
        for (const auto &opt : *cmd.options)
            arr.append(applicationCommandOptionToJson(opt));
        o.insert("options", arr);
    }
    if (cmd.version.hasValue())
        o.insert("version", cmd.version.get());
    return o;
}

QJsonObject interactionOptionToJson(const InteractionOptionValue &opt)
{
    QJsonObject o;
    o.insert("type", opt.type);
    o.insert("name", opt.name);
    if (opt.isScalar())
        o.insert("value", opt.value);
    if (!opt.options.isEmpty()) {
        QJsonArray arr;
        for (const auto &sub : opt.options)
            arr.append(interactionOptionToJson(sub));
        o.insert("options", arr);
    }
    return o;
}
} // namespace

void Client::sendApplicationCommandInteraction(Snowflake channelId, Snowflake guildId,
                                               const ApplicationCommand &command,
                                               const QList<InteractionOptionValue> &options,
                                               const QString &nonce)
{
    // Guard against invalid/unpopulated ids — otherwise they'd serialize as
    // "18446744073709551615" and the interaction would be rejected.
    if (!command.id.get().isValid() || !command.applicationId.get().isValid()) {
        qCWarning(LogDiscord) << "Refusing to send slash command" << command.name.get()
                              << "with invalid command/application id";
        return;
    }

    QJsonObject data;
    data.insert("id", command.id.get().toString());
    data.insert("name", command.name.get());
    data.insert("type", 1);
    if (command.version.hasValue())
        data.insert("version", command.version.get());
    if (!options.isEmpty()) {
        QJsonArray arr;
        for (const auto &opt : options)
            arr.append(interactionOptionToJson(opt));
        data.insert("options", arr);
    }
    data.insert("application_command", applicationCommandToJson(command));

    QJsonObject payload;
    payload.insert("type", 2);
    payload.insert("application_id", command.applicationId.get().toString());
    payload.insert("channel_id", channelId.toString());
    if (guildId.isValid())
        payload.insert("guild_id", guildId.toString());
    const QString session = gateway ? gateway->gatewaySessionId() : QString();
    if (session.isEmpty()) {
        // Without a gateway session_id the /interactions endpoint rejects the
        // payload; don't send an invalid request.
        qCWarning(LogDiscord) << "Refusing to send slash command" << command.name.get()
                              << "before gateway READY (no session_id)";
        return;
    }
    payload.insert("session_id", session);
    payload.insert("data", data);
    payload.insert("nonce", nonce.isEmpty() ? QString::number(Snowflake::generateNonce()) : nonce);

    httpClient->post("/interactions", payload, [command](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to send slash command" << command.name.get() << ":"
                                  << response.error;
    });
}

void Client::setUserNote(Snowflake userId, const QString &note)
{
    QString endpoint = "/users/@me/notes/" + QString::number(userId);
    QJsonObject payload;
    payload["note"] = note;

    httpClient->put(endpoint, payload, [userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to set note for user" << userId << ":"
                                  << response.error;
    });
}

void Client::fetchOwnProfile(GenericCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("type", "popout");
    query.addQueryItem("with_mutual_guilds", "false");
    query.addQueryItem("with_mutual_friends", "false");
    query.addQueryItem("with_mutual_friends_count", "false");

    httpClient->get("/users/@me/profile", query, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch own profile:" << response.error;
            callback({ {}, "Failed to fetch own profile: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

void Client::updateProfile(const QJsonObject &payload, GenericCallback callback)
{
    httpClient->patch("/users/@me", payload, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to update profile:" << response.error;
            callback({ {}, "Failed to update profile: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

void Client::fetchConnections(GenericCallback callback)
{
    httpClient->get("/users/@me/connections", QUrlQuery(), [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch connections:" << response.error;
            callback({ {}, "Failed to fetch connections: " + response.error });
            return;
        }
        QJsonObject obj;
        obj["connections"] = QJsonDocument::fromJson(response.body).array();
        callback({ obj });
    });
}

void Client::removeConnection(const QString &type, const QString &id, GenericCallback callback)
{
    QString endpoint = "/users/@me/connections/" + type + "/" + id;
    httpClient->delete_(endpoint, [type, id, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to remove connection" << type << id << ":"
                                  << response.error;
            callback({ {}, "Failed to remove connection: " + response.error });
            return;
        }
        callback({ QJsonObject{} });
    });
}

void Client::fetchAuthorizedApps(GenericCallback callback)
{
    httpClient->get("/oauth2/tokens", QUrlQuery(), [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch authorized apps:" << response.error;
            callback({ {}, "Failed to fetch authorized apps: " + response.error });
            return;
        }
        QJsonObject obj;
        obj["tokens"] = QJsonDocument::fromJson(response.body).array();
        callback({ obj });
    });
}

void Client::revokeAuthorizedApp(Snowflake appId, GenericCallback callback)
{
    QString endpoint = "/oauth2/tokens/" + QString::number(appId);
    httpClient->delete_(endpoint, [appId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to revoke authorized app" << appId << ":"
                                  << response.error;
            callback({ {}, "Failed to revoke authorized app: " + response.error });
            return;
        }
        callback({ QJsonObject{} });
    });
}

void Client::fetchPrivacySettings(GenericCallback callback)
{
    httpClient->get("/users/@me/privacy", QUrlQuery(), [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch privacy settings:" << response.error;
            callback({ {}, "Failed to fetch privacy settings: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

void Client::updatePrivacySettings(const QJsonObject &payload, GenericCallback callback)
{
    httpClient->patch("/users/@me/privacy", payload, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to update privacy settings:" << response.error;
            callback({ {}, "Failed to update privacy settings: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

void Client::updateCustomStatus(const QString &text, const QString &emojiName, qint64 expiresAt,
                                GenericCallback callback)
{
    QJsonObject customStatus;
    customStatus["text"] = text;
    if (!emojiName.isEmpty())
        customStatus["emoji_name"] = emojiName;
    if (expiresAt > 0)
        customStatus["expires_at"] = QString::number(expiresAt);

    QJsonObject payload;
    payload["custom_status"] = customStatus;

    httpClient->patch("/users/@me/settings", payload, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to update custom status:" << response.error;
            callback({ {}, "Failed to update custom status: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

void Client::clearCustomStatus(GenericCallback callback)
{
    QJsonObject payload;
    payload["custom_status"] = QJsonValue::Null;

    httpClient->patch("/users/@me/settings", payload, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to clear custom status:" << response.error;
            callback({ {}, "Failed to clear custom status: " + response.error });
            return;
        }
        callback({ QJsonDocument::fromJson(response.body).object() });
    });
}

namespace {

template <typename ResultT, typename ParseExtra>
void runThreadSearch(HttpClient *http, Core::Snowflake channelId, const QUrlQuery &query,
                     const QString &errorPrefix,
                     std::function<void(const Core::Result<ResultT> &)> callback,
                     ParseExtra parseExtra)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/threads/search";
    http->get(endpoint, query, [callback, parseExtra, errorPrefix](const HttpResponse &response) {
        if (response.statusCode == 202) {
            QJsonObject obj = QJsonDocument::fromJson(response.body).object();
            ResultT result;
            result.indexNotReady = true;
            result.retryAfterSeconds = qMax(1, qRound(obj.value("retry_after").toDouble(1.0)));
            callback(Core::Result<ResultT>::makeOk(result));
            return;
        }

        if (!response.success) {
            qCWarning(LogDiscord) << errorPrefix << response.error;
            callback(Core::Result<ResultT>::makeError(errorPrefix + " " + response.error));
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(response.body).object();
        ResultT result;
        result.hasMore = obj.value("has_more").toBool();
        for (const QJsonValue &val : obj.value("threads").toArray())
            result.threads.append(Channel::fromJson(val.toObject()));
        parseExtra(obj, result);
        callback(Core::Result<ResultT>::makeOk(result));
    });
}

} // namespace

void Client::searchForumThreads(Snowflake forumId, int offset, const QString &sortBy, ForumThreadsCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("sort_by", sortBy);
    query.addQueryItem("sort_order", "desc");
    query.addQueryItem("limit", "25");
    query.addQueryItem("tag_setting", "match_some");
    if (offset > 0)
        query.addQueryItem("offset", QString::number(offset));

    runThreadSearch<ForumThreadSearchResult>(
            httpClient, forumId, query, QStringLiteral("Failed to search forum threads:"),
            std::move(callback), [](const QJsonObject &obj, ForumThreadSearchResult &result) {
                for (const QJsonValue &val : obj.value("first_messages").toArray()) {
                    Message msg = Message::fromJson(val.toObject());
                    if (msg.channelId.hasValue())
                        result.firstMessages.insert(msg.channelId.get(), msg);
                }
            });
}

void Client::joinThread(Snowflake threadId)
{
    QString endpoint = "/channels/" + QString::number(threadId) + "/thread-members/@me";
    httpClient->put(endpoint, QJsonObject{}, [threadId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to join thread" << threadId << ":" << response.error;
    });
}

void Client::leaveThread(Snowflake threadId)
{
    QString endpoint = "/channels/" + QString::number(threadId) + "/thread-members/@me";
    httpClient->delete_(endpoint, [threadId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to leave thread" << threadId << ":" << response.error;
    });
}

void Client::searchThreads(Snowflake channelId, bool archived, int offset, ThreadListCallback callback)
{
    QUrlQuery query;
    query.addQueryItem("archived", archived ? "true" : "false");
    query.addQueryItem("sort_by", "last_message_time");
    query.addQueryItem("sort_order", "desc");
    query.addQueryItem("limit", "25");
    if (offset > 0)
        query.addQueryItem("offset", QString::number(offset));

    runThreadSearch<ThreadListResult>(
            httpClient, channelId, query, QStringLiteral("Failed to search threads:"),
            std::move(callback), [](const QJsonObject &obj, ThreadListResult &result) {
                for (const QJsonValue &val : obj.value("members").toArray())
                    result.members.append(ThreadMember::fromJson(val.toObject()));
            });
}

void Client::createForumThread(Snowflake forumId, const QString &name,
                               const QList<Snowflake> &appliedTags, const QString &content,
                               const QString &nonce,
                               const QList<Core::PendingAttachment> &attachments,
                               ForumThreadCallback callback)
{
    QJsonObject message;
    message["content"] = content;

    if (attachments.isEmpty()) {
        postForumThread(forumId, name, appliedTags, message, callback);
        return;
    }

    auto state = std::make_shared<UploadState>();
    state->channelId = forumId;
    state->nonce = nonce;
    state->attachments = attachments;
    state->onUploaded = [this, forumId, name, appliedTags, message, callback](const QJsonArray &attachmentsJson) {
        QJsonObject withFiles = message;
        withFiles["attachments"] = attachmentsJson;
        postForumThread(forumId, name, appliedTags, withFiles, callback);
    };
    state->onFailed = [callback](const QString &error) {
        if (callback)
            callback(Core::Result<CreatedForumThread>::makeError("Failed to create forum post: " + error));
    };

    for (int i = 0; i < attachments.size(); i++) {
        state->uploadFilenames.append(QString());
        state->uploaded.append(false);
    }
    state->cancelFlag = std::make_shared<std::atomic<bool>>(false);
    activeUploads.insert(nonce, state);
    uploadAttachmentsAndSend(state);
}

void Client::postForumThread(Snowflake forumId, const QString &name,
                             const QList<Snowflake> &appliedTags,
                             const QJsonObject &message,
                             ForumThreadCallback callback)
{
    QJsonArray tags;
    for (Snowflake tag : appliedTags)
        tags.append(QString::number(tag));

    QJsonObject body;
    body["name"] = name;
    body["auto_archive_duration"] = 1440;
    body["applied_tags"] = tags;
    body["message"] = message;

    QString endpoint = "/channels/" + QString::number(forumId) + "/threads?use_nested_fields=true";
    httpClient->post(endpoint, body, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to create forum thread:" << response.error;
            if (callback)
                callback(Core::Result<CreatedForumThread>::makeError(
                        "Failed to create forum thread: " + response.error));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(response.body).object();
        CreatedForumThread created;
        created.thread = Channel::fromJson(obj);
        if (obj.contains("message"))
            created.starterMessage = Message::fromJson(obj.value("message").toObject());
        if (callback)
            callback(Core::Result<CreatedForumThread>::makeOk(created));
    });
}

void Client::fetchForumPostData(Snowflake forumId, const QList<Snowflake> &threadIds,
                                ForumPostDataCallback callback)
{
    QJsonArray ids;
    for (Snowflake id : threadIds)
        ids.append(QString::number(id));

    QJsonObject body;
    body["thread_ids"] = ids;

    QString endpoint = "/channels/" + QString::number(forumId) + "/post-data";
    httpClient->post(endpoint, body, [callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch forum post data:" << response.error;
            callback(Core::Result<QHash<Snowflake, Message>>::makeError(
                    "Failed to fetch forum post data: " + response.error));
            return;
        }

        // { "threads": { "<threadId>": { "first_message": message|null, "owner": … } } }
        QHash<Snowflake, Message> firstMessages;
        QJsonObject threads = QJsonDocument::fromJson(response.body).object().value("threads").toObject();
        for (auto it = threads.constBegin(); it != threads.constEnd(); ++it) {
            QJsonValue fm = it.value().toObject().value("first_message");
            if (fm.isObject())
                firstMessages.insert(Snowflake(it.key().toULongLong()),
                                     Message::fromJson(fm.toObject()));
        }
        callback(Core::Result<QHash<Snowflake, Message>>::makeOk(firstMessages));
    });
}

void Client::onConnected()
{
    // TCP/WebSocket connect only — the session is not usable until READY.
    // Stay in Connecting so the UI doesn't show a premature connected state.
    qCInfo(LogDiscord) << "Gateway socket connected, waiting for READY";
}

void Client::onDisconnected(CloseCode code, const QString &reason)
{
    qWarning() << "Disconnected from gateway: " << code << reason;

    // Fatal close codes — no reconnection, transition straight to Disconnected
    if (code == CloseCode::AUTHENTICATION_FAILED ||
        code == CloseCode::INVALID_SHARD ||
        code == CloseCode::SHARDING_REQUIRED ||
        code == CloseCode::INVALID_API_VERSION ||
        code == CloseCode::INVALID_INTENTS ||
        code == CloseCode::DISALLOWED_INTENTS) {
        setState(Core::ConnectionState::Disconnected);
        if (code == CloseCode::AUTHENTICATION_FAILED) {
            emit errorOccurred("Invalid token");
            emit authenticationFailed();
        } else {
            emit errorOccurred("Fatal gateway error: " + reason);
        }
        return;
    }

    // User-initiated disconnect (via stop()) — transition to Disconnected
    if (state == Core::ConnectionState::Disconnecting) {
        setState(Core::ConnectionState::Disconnected);
        return;
    }

    // Gateway startup/terminal internal failures cannot be retried by this Client state.
    // Leave Connecting so the account UI can recover and the user can retry.
    if (code == CloseCode::INTERNAL) {
        setState(Core::ConnectionState::Disconnected);
        emit errorOccurred(reason.isEmpty()
                           ? QStringLiteral("Gateway connection failed")
                           : reason);
        return;
    }

    // Non-fatal: Gateway will handle reconnection automatically
    // stateChanged(Connecting) is emitted via the reconnecting signal
}

void Client::onGatewayReady(const Ready &data)
{
    for (const auto &guild : data.guilds.get())
        indexGuildMappings(guild);

    const QByteArray binary = QByteArray::fromBase64(data.userSettingsProto->toUtf8());
    Proto::ProtoReader reader(binary);
    settings = Proto::PreloadedUserSettings::fromProto(reader);

    if ((!settings.guildFolders.has_value() || settings.guildFolders->folders.isEmpty()) && data.userSettings.hasValue() && !data.userSettings->guildFolders->isEmpty())
        settings.guildFolders = guildFoldersFromLegacy(data.userSettings->guildFolders.get());

    me = data.user;

    setState(Core::ConnectionState::Connected);

    // identify() resets presence to "unknown"; re-apply the user's chosen status
    // (e.g. after a reconnect that fell back to a fresh identify).
    if (!m_lastPresenceStatus.isEmpty())
        gateway->sendPresenceUpdate(m_lastPresenceStatus);

    emit ready(data);
}

void Client::onGatewayReadySupplemental(const ReadySupplemental &data)
{
    emit readySupplemental(data);
}

void Client::onGatewayMessageCreate(const Message &msg)
{
    emit messageCreated(msg);
}

void Client::onGatewayMessageUpdate(const Message &msg)
{
    emit messageUpdated(msg);
}

void Client::onGatewayMessageDelete(const MessageDelete &event)
{
    emit messageDeleted(event);
}

void Client::onGatewayUserUpdate(const User &user)
{
    me = user;

    emit ownUserUpdated(user);
}

void Client::onGatewayChannelCreate(const ChannelCreate &event)
{
    emit channelCreated(event);
}

void Client::onGatewayGuildCreate(const GatewayGuild &guild)
{
    indexGuildMappings(guild);

    emit guildCreated(guild);
}

void Client::onGatewayGuildDelete(const GuildDelete &event)
{
    if (event.userRemoved() && event.id.hasValue())
        removeGuildMappings(event.id.get());

    emit guildDeleted(event);
}

void Client::indexGuildMappings(const GatewayGuild &guild)
{
    if (!guild.properties.hasValue())
        return;

    Snowflake guildId = guild.properties->id.get();
    if (guild.channels.hasValue())
        for (const auto &channel : guild.channels.get())
            channelToGuild.insert(channel.id, guildId);
    if (guild.threads.hasValue())
        for (const auto &thread : guild.threads.get())
            channelToGuild.insert(thread.id, guildId);
    guildPremiumTiers.insert(guildId, guild.properties->premiumTier.hasValue()
                                              ? guild.properties->premiumTier.get()
                                              : PremiumTier::NONE);
}

void Client::removeGuildMappings(Snowflake guildId)
{
    for (auto it = channelToGuild.begin(); it != channelToGuild.end();) {
        if (it.value() == guildId)
            it = channelToGuild.erase(it);
        else
            ++it;
    }
    guildPremiumTiers.remove(guildId);
}

void Client::onGatewayChannelUpdate(const ChannelUpdate &event)
{
    emit channelUpdated(event);
}

void Client::onGatewayChannelDelete(const ChannelDelete &event)
{
    emit channelDeleted(event);
}

void Client::onGatewayThreadCreate(const ChannelCreate &event)
{
    const Channel &thread = event.channel.get();
    Snowflake guildId = thread.guildId.hasValue() ? thread.guildId.get() : Snowflake::Invalid;
    if (!guildId.isValid() && thread.parentId.hasValue()) {
        auto it = channelToGuild.constFind(thread.parentId.get());
        if (it != channelToGuild.constEnd())
            guildId = it.value();
    }
    if (guildId.isValid())
        channelToGuild.insert(thread.id, guildId);

    emit threadCreated(event);
}

void Client::onGatewayThreadUpdate(const ChannelUpdate &event)
{
    const Channel &thread = event.channel.get();
    if (thread.guildId.hasValue())
        channelToGuild.insert(thread.id, thread.guildId.get());

    emit threadUpdated(event);
}

void Client::onGatewayThreadDelete(const ThreadDelete &event)
{
    channelToGuild.remove(event.id);

    emit threadDeleted(event);
}

void Client::onGatewayThreadListSync(const ThreadListSync &event)
{
    Snowflake guildId = event.guildId.get();
    if (event.threads.hasValue())
        for (const auto &thread : event.threads.get())
            channelToGuild.insert(thread.id, guildId);

    emit threadListSync(event);
}

void Client::onGatewayGuildRoleCreate(const GuildRoleCreate &event)
{
    emit guildRoleCreated(event);
}

void Client::onGatewayGuildRoleUpdate(const GuildRoleUpdate &event)
{
    emit guildRoleUpdated(event);
}

void Client::onGatewayGuildRoleDelete(const GuildRoleDelete &event)
{
    emit guildRoleDeleted(event);
}

void Client::sendSticker(Snowflake channelId, Snowflake stickerId)
{
    QJsonObject payload;
    QJsonArray stickerIds;
    stickerIds.append(QString::number(quint64(stickerId)));
    payload["sticker_ids"] = stickerIds;

    QString endpoint = "/channels/" + QString::number(channelId) + "/messages";
    httpClient->post(endpoint, payload, [this, channelId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to send sticker to channel" << channelId
                                  << ":" << response.error;
    });
}

void Client::sendMessage(Snowflake channelId, const QString &content, const QString &nonce,
                         Snowflake replyToMessageId, const QList<Core::PendingAttachment> &attachments)
{
    // todo extract to struct probably
    QJsonObject payload;
    payload["content"] = content;
    payload["flags"] = 0;
    payload["mobile_network_type"] = "unknown";
    payload["nonce"] = nonce;
    payload["tts"] = false;

    if (replyToMessageId.isValid()) {
        QJsonObject messageReference;
        messageReference["message_id"] = QString::number(replyToMessageId);
        messageReference["channel_id"] = QString::number(channelId);
        payload["message_reference"] = messageReference;
    }

    if (!attachments.isEmpty()) {
        auto state = std::make_shared<UploadState>();
        state->channelId = channelId;
        state->nonce = nonce;
        state->attachments = attachments;
        state->onUploaded = [this, channelId, nonce, payload](const QJsonArray &attachmentsJson) {
            QJsonObject withFiles = payload;
            withFiles["attachments"] = attachmentsJson;

            QString endpoint = "/channels/" + QString::number(channelId) + "/messages";
            httpClient->post(endpoint, withFiles, [this, nonce](const HttpResponse &response) {
                if (!response.success) {
                    qCWarning(LogDiscord) << "Failed to send message:" << response.error
                                          << "Status:" << response.statusCode;
                    emit messageSendFailed(nonce, response.error);
                }
            });
        };
        state->onFailed = [this, nonce](const QString &error) {
            emit messageSendFailed(nonce, error);
        };
        for (int i = 0; i < attachments.size(); i++) {
            state->uploadFilenames.append(QString());
            state->uploaded.append(false);
        }
        state->cancelFlag = std::make_shared<std::atomic<bool>>(false);
        activeUploads.insert(nonce, state);
        uploadAttachmentsAndSend(state);
        return;
    }

    QString endpoint = "/channels/" + QString::number(channelId) + "/messages";
    httpClient->post(endpoint, payload, [this, channelId, nonce](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to send message:" << response.error
                                  << "Status:" << response.statusCode;
            emit messageSendFailed(nonce, response.error);
            return;
        }

        qCInfo(LogDiscord) << "Message sent successfully to channel" << channelId;
    });
}

void Client::uploadAttachmentsAndSend(const std::shared_ptr<UploadState> &state)
{
    QJsonArray files;
    for (int i = 0; i < state->attachments.size(); i++) {
        QJsonObject file;
        file["id"] = QString::number(i);
        file["filename"] = state->attachments[i].filename;
        file["file_size"] = state->attachments[i].size;
        files.append(file);
    }
    QJsonObject body;
    body["files"] = files;

    QString endpoint = "/channels/" + QString::number(state->channelId) + "/attachments";
    httpClient->post(endpoint, body, [this, state](const HttpResponse &response) {
        if (state->cancelFlag->load()) {
            settleUpload(state);
            return;
        }
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to request upload slots:" << response.error
                                  << "Status:" << response.statusCode;
            settleUpload(state);
            failUpload(state, response.error);
            return;
        }

        const auto uploadSlots = QJsonDocument::fromJson(response.body).object()["attachments"].toArray();
        if (uploadSlots.size() != state->attachments.size()) {
            settleUpload(state);
            failUpload(state, "Unexpected upload slot response");
            return;
        }

        QStringList uploadUrls;
        for (int i = 0; i < state->attachments.size(); i++)
            uploadUrls.append(QString());
        for (const QJsonValue &slotValue : uploadSlots) {
            auto slot = slotValue.toObject();
            int index = slot["id"].toVariant().toInt();
            if (index < 0 || index >= state->attachments.size()) {
                // wut
                settleUpload(state);
                failUpload(state, "Unexpected upload slot response");
                return;
            }
            state->uploadFilenames[index] = slot["upload_filename"].toString();
            uploadUrls[index] = slot["upload_url"].toString();
        }

        state->remaining = state->attachments.size();
        for (int index = 0; index < state->attachments.size(); index++) {
            const auto &attachment = state->attachments[index];

            auto onDone = [this, state, index](const HttpResponse &putResponse) {
                state->remaining--;
                if (putResponse.success) {
                    state->uploaded[index] = true;
                } else if (!state->failed && !state->cancelFlag->load()) {
                    state->failed = true;
                    qCWarning(LogDiscord) << "Attachment upload failed:" << putResponse.error
                                          << "Status:" << putResponse.statusCode;
                    failUpload(state, putResponse.error);
                    state->cancelFlag->store(true); // abort !!!
                }
                if (state->remaining > 0)
                    return;

                if (state->failed || state->cancelFlag->load()) {
                    cleanupUploadedSlots(state);
                    settleUpload(state);
                    return;
                }
                finishUpload(state);
            };
            auto onProgress = [this, state, index](qint64 sent, qint64 total) {
                emit attachmentUploadProgress(state->nonce, index, sent, total);
            };

            // pasted bitmap from mem, otherwise from disk
            if (!attachment.data.isEmpty())
                httpClient->putExternal(uploadUrls[index], attachment.data, attachment.mimeType,
                                        onDone, onProgress, state->cancelFlag);
            else
                httpClient->putExternalFile(uploadUrls[index], attachment.filePath,
                                            attachment.mimeType, onDone, onProgress,
                                            state->cancelFlag);
        }
    });
}

void Client::finishUpload(const std::shared_ptr<UploadState> &state)
{
    if (state->cancelFlag->load()) {
        cleanupUploadedSlots(state);
        settleUpload(state);
        return;
    }

    QJsonArray attachmentsJson;
    for (int i = 0; i < state->attachments.size(); i++) {
        const auto &attachment = state->attachments[i];
        QJsonObject obj;
        obj["id"] = QString::number(i);
        obj["filename"] = attachment.filename;
        obj["uploaded_filename"] = state->uploadFilenames[i];
        if (attachment.isSpoiler)
            obj["is_spoiler"] = true;
        if (!attachment.description.isEmpty())
            obj["description"] = attachment.description;
        attachmentsJson.append(obj);
    }

    auto onUploaded = state->onUploaded;
    settleUpload(state);
    onUploaded(attachmentsJson);
}

void Client::failUpload(const std::shared_ptr<UploadState> &state, const QString &error)
{
    if (state->onFailed)
        state->onFailed(error);
}

void Client::cleanupUploadedSlots(const std::shared_ptr<UploadState> &state)
{
    for (int i = 0; i < state->uploaded.size(); i++) {
        if (!state->uploaded[i] || state->uploadFilenames[i].isEmpty())
            continue;
        const QString filename = state->uploadFilenames[i];
        httpClient->delete_("/attachments/" + filename, [filename](const HttpResponse &response) {
            if (!response.success)
                qCWarning(LogDiscord) << "Failed to clean up uploaded attachment slot" << filename
                                      << ":" << response.error << "Status:" << response.statusCode;
        });
    }
}

void Client::settleUpload(const std::shared_ptr<UploadState> &state)
{
    activeUploads.remove(state->nonce);
}

bool Client::cancelMessageSend(const QString &nonce)
{
    auto it = activeUploads.constFind(nonce);
    if (it == activeUploads.constEnd())
        return false;
    it.value()->cancelFlag->store(true);
    return true;
}

void Client::editMessage(Snowflake channelId, Snowflake messageId, const QString &content)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId);

    QJsonObject payload;
    payload["content"] = content;

    httpClient->patch(endpoint, payload, [this, channelId, messageId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to edit message" << messageId << "in channel"
                                  << channelId << ":" << response.error;
        else
            qCInfo(LogDiscord) << "Message" << messageId << "edited in channel" << channelId;
    });
}

void Client::deleteMessage(Snowflake channelId, Snowflake messageId)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId);

    httpClient->delete_(endpoint, [this, channelId, messageId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to delete message" << messageId << "in channel"
                                  << channelId << ":" << response.error;
        else
            qCInfo(LogDiscord) << "Message" << messageId << "deleted from channel" << channelId;
    });
}

void Client::pinMessage(Snowflake channelId, Snowflake messageId)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/pins/" +
                       QString::number(messageId);

    httpClient->put(endpoint, QJsonObject{}, [this, channelId, messageId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to pin message" << messageId << "in channel"
                                  << channelId << ":" << response.error;
        else
            qCInfo(LogDiscord) << "Message" << messageId << "pinned in channel" << channelId;
    });
}

void Client::unpinMessage(Snowflake channelId, Snowflake messageId,
                          std::function<void(bool success)> completion)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/pins/" +
                       QString::number(messageId);

    httpClient->delete_(endpoint,
                        [this, channelId, messageId, completion = std::move(completion)](
                                const HttpResponse &response) {
                            if (!response.success)
                                qCWarning(LogDiscord)
                                        << "Failed to unpin message" << messageId << "in channel"
                                        << channelId << ":" << response.error;
                            else
                                qCInfo(LogDiscord)
                                        << "Message" << messageId << "unpinned from channel"
                                        << channelId;
                            if (completion)
                                completion(response.success);
                        });
}

void Client::getPinnedMessages(Snowflake channelId, const MessagesCallback &callback)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/pins";

    httpClient->get(endpoint, QUrlQuery{}, [this, channelId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch pinned messages in channel" << channelId
                                  << ":" << response.error;
            callback({ {}, "Failed to fetch pinned messages: " + response.error });
            return;
        }

        QList<Message> results;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            results.append(Message::fromJson(val.toObject()));

        callback({ results });
    });
}

void Client::addReaction(Snowflake channelId, Snowflake messageId, const QString &emoji,
                         bool isBurst)
{
    QString encoded = QUrl::toPercentEncoding(emoji, ":");
    int type = isBurst ? 1 : 0;
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId) + "/reactions/" + encoded +
                       "/%40me?location=Message%20Inline%20Button&type=" + QString::number(type);

    httpClient->put(endpoint, QJsonObject{}, [this, channelId, messageId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to add reaction on message" << messageId
                                  << "in channel" << channelId << ":" << response.error;
    });
}

void Client::removeReaction(Snowflake channelId, Snowflake messageId, const QString &emoji,
                            bool isBurst)
{
    QString encoded = QUrl::toPercentEncoding(emoji, ":");
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId) + "/reactions/" + encoded + "/%40me";
    if (isBurst)
        endpoint += "?burst=true";
    else
        endpoint += "?type=0";

    httpClient->delete_(endpoint, [this, channelId, messageId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to remove reaction on message" << messageId
                                  << "in channel" << channelId << ":" << response.error;
    });
}

void Client::votePoll(Core::Snowflake channelId, Core::Snowflake messageId, Core::Snowflake pollId,
                      const QList<int> &answerIds, PollVoteCallback callback)
{
    // Discord treats the message id as the poll id: the message entity's poll
    // object carries no separate identifier.
    const Snowflake effectivePollId = pollId.isValid() ? pollId : messageId;

    QJsonArray answerIdArray;
    for (int answerId : answerIds) {
        QJsonObject entry;
        entry["answer_id"] = answerId;
        answerIdArray.append(entry);
    }

    QJsonObject body;
    body["answer_ids"] = answerIdArray;

    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId) + "/polls/" +
                       QString::number(effectivePollId) + "/answers";

    httpClient->post(endpoint, body,
                     [this, channelId, messageId, callback = std::move(callback)](
                             const HttpResponse &response) {
                         if (!response.success) {
                             qCWarning(LogDiscord) << "Failed to vote on poll on message"
                                                   << messageId << "in channel" << channelId
                                                   << ":" << response.error;
                             if (callback)
                                 callback(Core::Result<Message>::makeError(
                                         "Failed to vote on poll: " + response.error));
                             return;
                         }

                         // The endpoint returns the updated message; relay it so
                         // callers can refresh poll results. Tolerate an empty
                         // body (204-style) by still reporting success.
                         if (callback) {
                             const QJsonDocument doc = QJsonDocument::fromJson(response.body);
                             if (doc.isObject())
                                 callback(Core::Result<Message>::makeOk(
                                         Message::fromJson(doc.object())));
                             else
                                 callback(Core::Result<Message>::makeOk(Message{}));
                         }
                     });
}

void Client::leaveGuild(Snowflake guildId)
{
    QString endpoint = "/users/@me/guilds/" + QString::number(guildId);
    httpClient->delete_(endpoint, QJsonObject{}, [this, guildId](const HttpResponse &response) {
        if (!response.success) {
            QString err = QStringLiteral("status=%1 error=%2")
                                  .arg(response.statusCode)
                                  .arg(response.error);
            qCWarning(LogDiscord) << "Failed to leave guild" << guildId << err;
            emit guildLeaveFailed(guildId, err);
        }
    });
}

#ifndef QT_NO_DEBUG
void Client::debugForceReconnect()
{
    gateway->debugForceReconnect();
}
#endif

void Client::ackMessage(Snowflake channelId, Snowflake messageId, int flags, int lastViewed)
{
    QString endpoint = "/channels/" + QString::number(channelId) + "/messages/" +
                       QString::number(messageId) + "/ack";

    QJsonObject payload;
    payload["flags"] = flags;
    payload["last_viewed"] = lastViewed;
    payload["token"] = QJsonValue::Null;

    httpClient->post(endpoint, payload, [this, channelId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to ack message in channel" << channelId
                                  << ":" << response.error;
    });
}

void Client::ackBulk(const QList<AckEntry> &entries)
{
    QJsonArray readStates;
    for (const auto &entry : entries) {
        QJsonObject obj;
        obj["channel_id"] = QString::number(entry.channelId);
        obj["message_id"] = QString::number(entry.messageId);
        obj["read_state_type"] = entry.readStateType;
        readStates.append(obj);
    }

    QJsonObject payload;
    payload["read_states"] = readStates;
    httpClient->post("/read-states/ack-bulk", payload, [this](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to bulk ack:" << response.error;
    });
}

void Client::subscribeToGuildChannel(Snowflake guildId, Snowflake channelId,
                                     const QList<QPair<int, int>> &ranges)
{
    gateway->subscribeToGuild(guildId, channelId, ranges);
    subscribedGuilds.insert(guildId);
}

void Client::ensureSubscriptionByChannel(Snowflake channelId)
{
    if (!channelToGuild.contains(channelId))
        return;

    Snowflake guildId = channelToGuild.value(channelId);
    if (!subscribedGuilds.contains(guildId)) {
        QList<QPair<int, int>> defaultRanges = { { 0, 99 } };
        subscribeToGuildChannel(guildId, channelId, defaultRanges);
    }
}

void Client::requestForumUnreads(Snowflake forumId, const QList<QPair<Snowflake, Snowflake>> &threads)
{
    if (threads.isEmpty())
        return;

    Snowflake guildId = getGuildIdForChannel(forumId);
    if (!guildId.isValid())
        return;

    gateway->requestForumUnreads(guildId, forumId, threads);
}

Snowflake Client::getGuildIdForChannel(Snowflake channelId) const
{
    return channelToGuild.value(channelId, Snowflake::Invalid);
}

PremiumTier Client::getGuildPremiumTier(Snowflake guildId) const
{
    return guildPremiumTiers.value(guildId, PremiumTier::NONE);
}

qint64 Client::getMaxUploadSize(Snowflake channelId) const
{
    constexpr qint64 MiB = 1024 * 1024;

    auto premiumType = me.premiumType.hasValue() ? me.premiumType.get() : PremiumType::NONE;
    qint64 userLimit = 10 * MiB;
    switch (premiumType) {
    case PremiumType::TIER_1:
        userLimit = 50 * MiB;
        break;
    case PremiumType::TIER_2:
        userLimit = 500 * MiB;
        break;
    case PremiumType::TIER_3:
        userLimit = 50 * MiB;
        break;
    default:
        break;
    }

    qint64 guildLimit = 10 * MiB;
    Snowflake guildId = getGuildIdForChannel(channelId);
    if (guildId.isValid()) {
        switch (getGuildPremiumTier(guildId)) {
        case PremiumTier::TIER_2:
            guildLimit = 50 * MiB;
            break;
        case PremiumTier::TIER_3:
            guildLimit = 100 * MiB;
            break;
        default:
            break;
        }
    }

    return qMax(userLimit, guildLimit);
}

void Client::sendVoiceStateUpdate(Snowflake guildId, Snowflake channelId, bool selfMute, bool selfDeaf)
{
    gateway->sendVoiceStateUpdate(guildId, channelId, selfMute, selfDeaf);
}

void Client::setPresenceStatus(const QString &status)
{
    m_lastPresenceStatus = status;
    gateway->sendPresenceUpdate(status);
}

void Client::requestGuildMembers(Snowflake guildId, const QList<Snowflake> &userIds)
{
    gateway->requestGuildMembers(guildId, userIds);
}

void Client::sendFriendRequest(const QString &username, const QString &tag)
{
    QJsonObject payload;
    payload["username"] = username;
    if (!tag.isEmpty())
        payload["discriminator"] = tag;

    httpClient->post("/users/@me/relationships", payload, [this, username](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to send friend request to" << username
                                  << ":" << response.error;
    });
}

void Client::acceptFriendRequest(Snowflake userId)
{
    QString endpoint = "/users/@me/relationships/" + QString::number(userId);
    httpClient->put(endpoint, QJsonObject{}, [this, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to accept friend request from" << userId
                                  << ":" << response.error;
    });
}

void Client::removeFriend(Snowflake userId)
{
    QString endpoint = "/users/@me/relationships/" + QString::number(userId);
    httpClient->delete_(endpoint, [this, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to remove relationship with" << userId
                                  << ":" << response.error;
    });
}

void Client::blockUser(Snowflake userId)
{
    QJsonObject payload;
    payload["type"] = static_cast<int>(Discord::RelationshipType::BLOCKED);

    QString endpoint = "/users/@me/relationships/" + QString::number(userId);
    httpClient->put(endpoint, payload, [this, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to block user" << userId
                                  << ":" << response.error;
    });
}

[[nodiscard]] const Proto::PreloadedUserSettings &Client::getSettings() const
{
    return settings;
}

[[nodiscard]] const User &Client::getMe() const
{
    return me;
}

void Client::setState(Core::ConnectionState state)
{
    if (this->state != state) {
        this->state = state;
        emit stateChanged(state);
    }
}

// === Guild management REST API ===

void Client::fetchGuildBans(Snowflake guildId, BansCallback callback)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/bans";
    QUrlQuery query;
    query.addQueryItem("limit", "1000");

    httpClient->get(endpoint, query, [this, guildId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch guild bans:" << response.error;
            callback({ {}, "Failed to fetch bans: " + response.error });
            return;
        }

        QList<BanEntry> bans;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            bans.append(BanEntry::fromJson(val.toObject()));

        emit guildBansFetched(guildId, bans);
        callback({ bans });
    });
}

void Client::unbanMember(Snowflake guildId, Snowflake userId)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/bans/" + QString::number(userId);
    httpClient->delete_(endpoint, [guildId, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to unban user" << userId << "from guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::banMember(Snowflake guildId, Snowflake userId, int deleteMessageSeconds,
                       const QString &reason)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/bans/" + QString::number(userId);

    QJsonObject body;
    if (deleteMessageSeconds > 0)
        body.insert("delete_message_seconds", deleteMessageSeconds);

    QList<QPair<QString, QString>> headers;
    if (!reason.isEmpty())
        headers.append({ "X-Audit-Log-Reason", reason });

    httpClient->put(endpoint, body, headers, [guildId, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to ban user" << userId << "from guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::kickMember(Snowflake guildId, Snowflake userId, const QString &reason)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/members/" + QString::number(userId);

    QList<QPair<QString, QString>> headers;
    if (!reason.isEmpty())
        headers.append({ "X-Audit-Log-Reason", reason });

    httpClient->delete_(endpoint, headers, [guildId, userId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to kick user" << userId << "from guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::tempBanMember(Snowflake guildId, Snowflake userId, int deleteMessageSeconds,
                           const QString &reason, int durationSeconds)
{
    if (durationSeconds <= 0) {
        qCWarning(LogDiscord) << "tempBanMember: invalid duration" << durationSeconds;
        return;
    }

    banMember(guildId, userId, deleteMessageSeconds, reason);

    // Discord has no native "temp ban"; ban now and schedule the unban.
    // Use the qint64 overload to avoid int32 overflow for long durations.
    const qint64 delayMs = static_cast<qint64>(durationSeconds) * 1000;
    const qint64 unbanAtMs = QDateTime::currentMSecsSinceEpoch() + delayMs;

    // Cancel any previously armed unban for the same (guild, user) so re-issuing
    // a temp ban (or a restore colliding with a fresh ban) doesn't unban early.
    const QPair<Snowflake, Snowflake> key(guildId, userId);
    if (auto it = m_pendingUnbanTimers.find(key); it != m_pendingUnbanTimers.end()) {
        if (it.value())
            it.value()->stop();
        m_pendingUnbanTimers.erase(it);
    }

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, key, guildId, userId]() {
        m_pendingUnbanTimers.remove(key);
        unbanMember(guildId, userId);
        removePendingUnban(guildId, userId);
    });
    timer->start(delayMs);
    m_pendingUnbanTimers.insert(key, timer);

    schedulePendingUnban(guildId, userId, unbanAtMs);
}

void Client::restorePendingUnbans()
{
    QSettings settings;
    const QVariantList list = settings.value("moderation/pending_unbans").toList();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const auto &v : list) {
        const QVariantMap e = v.toMap();
        const Snowflake guildId(e.value("guild").toString().toULongLong());
        const Snowflake userId(e.value("user").toString().toULongLong());
        const qint64 at = e.value("at").toLongLong();
        if (!guildId.isValid() || !userId.isValid() || at <= 0)
            continue;
        if (at <= now) {
            unbanMember(guildId, userId);
            removePendingUnban(guildId, userId);
            continue;
        }
        const qint64 delayMs = at - now;
        const QPair<Snowflake, Snowflake> key(guildId, userId);
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, key, guildId, userId]() {
            m_pendingUnbanTimers.remove(key);
            unbanMember(guildId, userId);
            removePendingUnban(guildId, userId);
        });
        timer->start(delayMs);
        m_pendingUnbanTimers.insert(key, timer);
    }
}

void Client::schedulePendingUnban(Snowflake guildId, Snowflake userId, qint64 unbanAtMs)
{
    QSettings settings;
    QVariantList list = settings.value("moderation/pending_unbans").toList();
    // Dedup: replace any existing entry for the same (guild, user).
    const QString guildStr = guildId.toString();
    const QString userStr = userId.toString();
    for (auto it = list.begin(); it != list.end();) {
        const QVariantMap e = it->toMap();
        if (e.value("guild").toString() == guildStr && e.value("user").toString() == userStr)
            it = list.erase(it);
        else
            ++it;
    }
    QVariantMap entry;
    entry["guild"] = guildStr;
    entry["user"] = userStr;
    entry["at"] = unbanAtMs;
    list.append(entry);
    settings.setValue("moderation/pending_unbans", list);
}

void Client::removePendingUnban(Snowflake guildId, Snowflake userId)
{
    QSettings settings;
    QVariantList list = settings.value("moderation/pending_unbans").toList();
    QVariantList kept;
    for (const auto &v : list) {
        const QVariantMap e = v.toMap();
        if (e.value("guild").toString() == guildId.toString()
            && e.value("user").toString() == userId.toString())
            continue;
        kept.append(v);
    }
    settings.setValue("moderation/pending_unbans", kept);
}

void Client::setMemberMute(Snowflake guildId, Snowflake userId, bool muted, const QString &reason)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/members/" + QString::number(userId);

    QJsonObject body;
    body.insert("mute", muted);

    QList<QPair<QString, QString>> headers;
    if (!reason.isEmpty())
        headers.append({ "X-Audit-Log-Reason", reason });

    httpClient->patch(endpoint, body, headers, [guildId, userId, muted](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to set mute" << muted << "for user" << userId
                                  << "in guild" << guildId << ":" << response.error;
    });
}

void Client::setMemberDeaf(Snowflake guildId, Snowflake userId, bool deafened,
                           const QString &reason)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/members/" + QString::number(userId);

    QJsonObject body;
    body.insert("deaf", deafened);

    QList<QPair<QString, QString>> headers;
    if (!reason.isEmpty())
        headers.append({ "X-Audit-Log-Reason", reason });

    httpClient->patch(endpoint, body, headers,
                      [guildId, userId, deafened](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to set deaf" << deafened << "for user" << userId
                                  << "in guild" << guildId << ":" << response.error;
    });
}

void Client::setMemberTimeout(Snowflake guildId, Snowflake userId, int durationSeconds,
                              const QString &reason)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/members/" + QString::number(userId);

    QJsonObject body;
    if (durationSeconds > 0) {
        const QString until = QDateTime::currentDateTimeUtc()
                                      .addSecs(durationSeconds)
                                      .toString(Qt::ISODate);
        body.insert("communication_disabled_until", until);
    } else {
        body.insert("communication_disabled_until", QJsonValue::Null);
    }

    QList<QPair<QString, QString>> headers;
    if (!reason.isEmpty())
        headers.append({ "X-Audit-Log-Reason", reason });

    httpClient->patch(endpoint, body, headers,
                      [guildId, userId, durationSeconds](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to timeout user" << userId << "in guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::fetchGuildInvites(Snowflake guildId, InvitesCallback callback)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/invites";

    httpClient->get(endpoint, {}, [this, guildId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch guild invites:" << response.error;
            callback({ {}, "Failed to fetch invites: " + response.error });
            return;
        }

        QList<InviteData> invites;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            invites.append(InviteData::fromJson(val.toObject()));

        emit guildInvitesFetched(guildId, invites);
        callback({ invites });
    });
}

void Client::revokeInvite(Snowflake channelId, const QString &code)
{
    Q_UNUSED(channelId);
    QString endpoint = "/invites/" + code;
    httpClient->delete_(endpoint, [code](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to revoke invite" << code
                                  << ":" << response.error;
    });
}

void Client::fetchGuildAuditLog(Snowflake guildId, Snowflake userIdFilter,
                                 Snowflake actionTypeFilter, Snowflake beforeId,
                                 int limit, AuditLogCallback callback)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/audit-logs";
    QUrlQuery query;
    if (userIdFilter.isValid())
        query.addQueryItem("user_id", QString::number(userIdFilter));
    if (actionTypeFilter.isValid())
        query.addQueryItem("action_type", QString::number(actionTypeFilter));
    if (beforeId.isValid())
        query.addQueryItem("before", QString::number(beforeId));
    if (limit > 0)
        query.addQueryItem("limit", QString::number(limit));

    httpClient->get(endpoint, query, [this, guildId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch audit log:" << response.error;
            callback({ {}, "Failed to fetch audit log: " + response.error });
            return;
        }

        QJsonObject obj = QJsonDocument::fromJson(response.body).object();
        AuditLogData data = AuditLogData::fromJson(obj);
        emit auditLogFetched(guildId, data);
        callback({ data });
    });
}

void Client::fetchGuildWebhooks(Snowflake guildId, WebhooksCallback callback)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/webhooks";

    httpClient->get(endpoint, {}, [this, guildId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch guild webhooks:" << response.error;
            callback({ {}, "Failed to fetch webhooks: " + response.error });
            return;
        }

        QList<WebhookData> webhooks;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            webhooks.append(WebhookData::fromJson(val.toObject()));

        emit guildWebhooksFetched(guildId, webhooks);
        callback({ webhooks });
    });
}

void Client::createWebhook(Snowflake channelId, const QString &name, const QString &avatar)
{
    QJsonObject payload;
    payload["name"] = name;
    if (!avatar.isEmpty())
        payload["avatar"] = avatar;

    QString endpoint = "/channels/" + QString::number(channelId) + "/webhooks";
    httpClient->post(endpoint, payload, [channelId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to create webhook in channel" << channelId
                                  << ":" << response.error;
    });
}

void Client::deleteWebhook(Snowflake webhookId)
{
    QString endpoint = "/webhooks/" + QString::number(webhookId);
    httpClient->delete_(endpoint, [webhookId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to delete webhook" << webhookId
                                  << ":" << response.error;
    });
}

void Client::modifyWebhook(Snowflake webhookId, const QString &name, const QString &channelId)
{
    QJsonObject payload;
    payload["name"] = name;
    if (!channelId.isEmpty())
        payload["channel_id"] = channelId;

    QString endpoint = "/webhooks/" + QString::number(webhookId);
    httpClient->patch(endpoint, payload, [webhookId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to modify webhook" << webhookId
                                  << ":" << response.error;
    });
}

void Client::fetchGuildIntegrations(Snowflake guildId, IntegrationsCallback callback)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/integrations";

    httpClient->get(endpoint, {}, [this, guildId, callback](const HttpResponse &response) {
        if (!response.success) {
            qCWarning(LogDiscord) << "Failed to fetch guild integrations:" << response.error;
            callback({ {}, "Failed to fetch integrations: " + response.error });
            return;
        }

        QList<IntegrationData> integrations;
        QJsonArray arr = QJsonDocument::fromJson(response.body).array();
        for (const QJsonValue &val : arr)
            integrations.append(IntegrationData::fromJson(val.toObject()));

        emit guildIntegrationsFetched(guildId, integrations);
        callback({ integrations });
    });
}

void Client::deleteIntegration(Snowflake guildId, Snowflake integrationId)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/integrations/" + QString::number(integrationId);
    httpClient->delete_(endpoint, [guildId, integrationId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to delete integration" << integrationId
                                  << "from guild" << guildId << ":" << response.error;
    });
}

void Client::modifyRole(Snowflake guildId, Snowflake roleId, const Role &role)
{
    QJsonObject payload;
    payload["name"] = role.name.get();
    payload["permissions"] = QString::number(static_cast<qint64>(role.permissions.get().toInt()));
    if (role.color.hasValue())
        payload["color"] = role.color.get();
    if (role.hoist.hasValue())
        payload["hoist"] = role.hoist.get();
    if (role.mentionable.hasValue())
        payload["mentionable"] = role.mentionable.get();

    QString endpoint = "/guilds/" + QString::number(guildId) + "/roles/" + QString::number(roleId);
    httpClient->patch(endpoint, payload, [guildId, roleId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to modify role" << roleId
                                  << "in guild" << guildId << ":" << response.error;
    });
}

void Client::createRole(Snowflake guildId, const QString &name, Permissions permissions,
                        int color, bool hoist, bool mentionable)
{
    QJsonObject payload;
    payload["name"] = name;
    payload["permissions"] = QString::number(static_cast<qint64>(permissions.toInt()));
    payload["color"] = color;
    payload["hoist"] = hoist;
    payload["mentionable"] = mentionable;

    QString endpoint = "/guilds/" + QString::number(guildId) + "/roles";
    httpClient->post(endpoint, payload, [guildId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to create role in guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::deleteRole(Snowflake guildId, Snowflake roleId)
{
    QString endpoint = "/guilds/" + QString::number(guildId) + "/roles/" + QString::number(roleId);
    httpClient->delete_(endpoint, [guildId, roleId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to delete role" << roleId
                                  << "in guild" << guildId << ":" << response.error;
    });
}

void Client::reorderRoles(Snowflake guildId, const QList<QPair<Snowflake, int>> &rolePositions)
{
    QJsonArray arr;
    for (const auto &pair : rolePositions) {
        QJsonObject item;
        item["id"] = QString::number(pair.first);
        item["position"] = pair.second;
        arr.append(item);
    }

    QString endpoint = "/guilds/" + QString::number(guildId) + "/roles";
    httpClient->patch(endpoint, arr, [guildId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to reorder roles in guild" << guildId
                                  << ":" << response.error;
    });
}

void Client::modifyGuild(Snowflake guildId, const QString &name)
{
    QJsonObject payload;
    payload["name"] = name;

    QString endpoint = "/guilds/" + QString::number(guildId);
    httpClient->patch(endpoint, payload, [guildId](const HttpResponse &response) {
        if (!response.success)
            qCWarning(LogDiscord) << "Failed to modify guild" << guildId
                                  << ":" << response.error;
    });
}

} // namespace Discord
} // namespace Acheron

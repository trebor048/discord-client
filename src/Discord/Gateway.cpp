#include "Gateway.hpp"

#include "Enums.hpp"
#include "Objects.hpp"
#include "Outbound.hpp"
#include "Inbound.hpp"
#include "Events.hpp"
#include "CurlUtils.hpp"
#include "ClientIdentity.hpp"

#include "Core/Logging.hpp"
#include "Proto/ProtoReader.hpp"
#include "Proto/UserSettings.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QUrl>

#include <algorithm>
#include <random>

namespace Acheron {
namespace Discord {

namespace {
// Discord's documented default heartbeat interval (ms), used when a malformed
// HELLO omits or zeroes the server-provided interval.
constexpr int kDefaultHeartbeatInterval = 41'250;
} // namespace

Gateway::Gateway(const QString &token, const QString &gatewayUrl, ClientIdentity &identity,
                 QObject *parent)
    : QObject(parent), token(token), gatewayUrl(gatewayUrl), identity(identity), running(false)
{
}

Gateway::~Gateway()
{
    hardStop();
}

void Gateway::start()
{
    if (running) {
        qCWarning(LogDiscord) << "Attempt to start already running gateway";
        return;
    }

    wantToClose = false;

    ingest = new IngestThread(this);
    connect(ingest, &IngestThread::payloadReceived, this, &Gateway::onPayloadReceived);
    // The signal fires on the ingest worker thread; a DirectConnection sets the
    // atomic ack flag synchronously on that thread, so a main-thread stall
    // (channel/guild switch, DB IO) can no longer starve the heartbeat ACK.
    connect(ingest, &IngestThread::heartbeatAckReceived, this,
            [this] { heartbeatAckReceived = true; }, Qt::DirectConnection);
    connect(ingest, &IngestThread::decompressionError, this, [this] {
        qCWarning(LogDiscord) << "Decompression error — forcing reconnect";
        shouldReconnect = true;
    });

    ingest->start();

    running = true;
    networkThread = std::thread(&Gateway::networkLoop, this);
}

void Gateway::stop()
{
    wantToClose = true;
    closeTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count();

    if (ingest)
        ingest->stop();
    // Do not deleteLater here — the network thread may still call ingest->push()
    // during the close-handshake window. Ingest is cleaned up in hardStop()
    // (which joins the network thread first) or via Qt child cleanup in the destructor.
}

void Gateway::hardStop()
{
    shouldReconnect = false;
    running = false;

    heartbeatCv.notify_all();
    if (networkThread.joinable())
        networkThread.join();
    {
        std::lock_guard lock(heartbeatThreadMutex);
        if (heartbeatThread.joinable())
            heartbeatThread.join();
    }
    if (ingest)
        ingest->stop();
}

void Gateway::subscribeToGuild(Core::Snowflake guildId, Core::Snowflake channelId, const QList<QPair<int, int>> &ranges)
{
    GuildSubscriptionsBulk data;
    GuildSubscriptionsBulk::SubscriptionData guild;
    guild.typing = true;
    guild.activities = true;
    guild.threads = true;
    guild.channels.insert(channelId, ranges);
    data.subscriptions.get().insert(guildId, guild);

    qCDebug(LogDiscord) << "Subscribing to channel" << channelId << "with ranges" << ranges;

    sendPayload(data.toJson());
}

void Gateway::sendPayload(const QJsonObject &obj)
{
    sendPayload(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

void Gateway::sendPayload(const QByteArray &data)
{
    const bool ok =
            CurlUtils::wsSend(curl, curlMutex, data.constData(), data.size(), CURLWS_TEXT, "gateway");
    if (!ok) {
        // A blocked/partial frame leaves the outbound WS stream corrupt: the
        // peer either closes it (decode error) or the connection hangs until
        // the heartbeat zombie detector fires. Reconnect immediately instead —
        // the inbound session is untouched, so the reconnect resumes cleanly.
        qCWarning(LogDiscord) << "Gateway send failed — forcing reconnect";
        shouldReconnect = true;
    }
}

void Gateway::onPayloadReceived(const QJsonObject &root)
{
    Inbound msg = Inbound::fromJson(root);

    qCDebug(LogDiscord) << "Received payload"
                        << "op" << static_cast<int>(msg.opcode) << "t" << msg.t.value_or(QString())
                        << "s" << msg.s.value_or(-1);

    if (msg.s.has_value())
        lastReceivedSequence = msg.s.value();

    switch (msg.opcode) {
    case OpCode::DISPATCH:
        handleDispatch(msg);
        break;
    case OpCode::HELLO:
        handleHello(msg);
        break;
    case OpCode::HEARTBEAT_ACK:
        heartbeatAckReceived = true;
        break;
    case OpCode::RECONNECT:
        qCInfo(LogDiscord) << "Server requested reconnect";
        shouldReconnect = true;
        break;
    case OpCode::INVALID_SESSION: {
        // Discord defaults the resumable flag to true when `d` is absent.
        const bool resumable =
                msg.data.isUndefined() ? true : msg.data.toBool();
        qCInfo(LogDiscord) << "Invalid session, resumable:" << resumable;
        if (!resumable) {
            canResume = false;
            std::lock_guard lock(sessionMutex);
            sessionId.clear();
        }
        shouldReconnect = true;
        break;
    }
    default:
        break;
    }
}

void Gateway::handleDispatch(const Inbound &data)
{
    QString t = data.t.value_or("");
    qCDebug(LogDiscord) << "Received dispatch event" << t;

    GatewayEvent event = parseGatewayEvent(t);

    switch (event) {
    case GatewayEvent::READY:
        handleReady(data);
        break;
    case GatewayEvent::READY_SUPPLEMENTAL:
        handleReadySupplemental(data);
        break;
    case GatewayEvent::MESSAGE_CREATE:
        handleMessageCreate(data);
        break;
    case GatewayEvent::MESSAGE_UPDATE:
        handleMessageUpdate(data);
        break;
    case GatewayEvent::MESSAGE_DELETE:
        handleMessageDelete(data);
        break;
    case GatewayEvent::MESSAGE_DELETE_BULK:
        handleMessageDeleteBulk(data);
        break;
    case GatewayEvent::TYPING_START:
        handleTypingStart(data);
        break;
    case GatewayEvent::CHANNEL_CREATE:
        handleChannelCreate(data);
        break;
    case GatewayEvent::CHANNEL_UPDATE:
        handleChannelUpdate(data);
        break;
    case GatewayEvent::CHANNEL_DELETE:
        handleChannelDelete(data);
        break;
    case GatewayEvent::THREAD_CREATE:
        handleThreadCreate(data);
        break;
    case GatewayEvent::THREAD_UPDATE:
        handleThreadUpdate(data);
        break;
    case GatewayEvent::THREAD_DELETE:
        handleThreadDelete(data);
        break;
    case GatewayEvent::THREAD_LIST_SYNC:
        handleThreadListSync(data);
        break;
    case GatewayEvent::THREAD_MEMBER_UPDATE:
        handleThreadMemberUpdate(data);
        break;
    case GatewayEvent::THREAD_MEMBERS_UPDATE:
        handleThreadMembersUpdate(data);
        break;
    case GatewayEvent::FORUM_UNREADS:
        handleForumUnreads(data);
        break;
    case GatewayEvent::GUILD_CREATE:
        handleGuildCreate(data);
        break;
    case GatewayEvent::GUILD_DELETE:
        handleGuildDelete(data);
        break;
    case GatewayEvent::GUILD_MEMBERS_CHUNK:
        handleGuildMembersChunk(data);
        break;
    case GatewayEvent::GUILD_MEMBER_UPDATE:
        handleGuildMemberUpdate(data);
        break;
    case GatewayEvent::GUILD_MEMBER_ADD:
        handleGuildMemberAdd(data);
        break;
    case GatewayEvent::GUILD_MEMBER_REMOVE:
        handleGuildMemberRemove(data);
        break;
    case GatewayEvent::USER_UPDATE:
        handleUserUpdate(data);
        break;
    case GatewayEvent::GUILD_ROLE_CREATE:
        handleGuildRoleCreate(data);
        break;
    case GatewayEvent::GUILD_ROLE_UPDATE:
        handleGuildRoleUpdate(data);
        break;
    case GatewayEvent::GUILD_ROLE_DELETE:
        handleGuildRoleDelete(data);
        break;
    case GatewayEvent::MESSAGE_ACK:
        handleMessageAck(data);
        break;
    case GatewayEvent::MESSAGE_REACTION_ADD:
        handleMessageReactionAdd(data);
        break;
    case GatewayEvent::MESSAGE_REACTION_ADD_MANY:
        handleMessageReactionAddMany(data);
        break;
    case GatewayEvent::MESSAGE_REACTION_REMOVE:
        handleMessageReactionRemove(data);
        break;
    case GatewayEvent::MESSAGE_REACTION_REMOVE_ALL:
        handleMessageReactionRemoveAll(data);
        break;
    case GatewayEvent::MESSAGE_REACTION_REMOVE_EMOJI:
        handleMessageReactionRemoveEmoji(data);
        break;
    case GatewayEvent::USER_GUILD_SETTINGS_UPDATE:
        handleUserGuildSettingsUpdate(data);
        break;
    case GatewayEvent::GUILD_MEMBER_LIST_UPDATE:
        handleGuildMemberListUpdate(data);
        break;
    case GatewayEvent::VOICE_STATE_UPDATE:
        handleVoiceStateUpdate(data);
        break;
    case GatewayEvent::VOICE_STATE_UPDATE_BATCH:
        handleVoiceStateUpdateBatch(data);
        break;
    case GatewayEvent::VOICE_SERVER_UPDATE:
        handleVoiceServerUpdate(data);
        break;
    case GatewayEvent::RELATIONSHIP_ADD:
        handleRelationshipAdd(data);
        break;
    case GatewayEvent::RELATIONSHIP_UPDATE:
        handleRelationshipUpdate(data);
        break;
    case GatewayEvent::RELATIONSHIP_REMOVE:
        handleRelationshipRemove(data);
        break;
    case GatewayEvent::USER_NOTE_UPDATE:
        handleUserNoteUpdate(data);
        break;
    case GatewayEvent::GUILD_UPDATE:
        handleGuildUpdate(data);
        break;
    case GatewayEvent::GUILD_BAN_ADD:
        handleGuildBanAdd(data);
        break;
    case GatewayEvent::GUILD_BAN_REMOVE:
        handleGuildBanRemove(data);
        break;
    case GatewayEvent::GUILD_EMOJIS_UPDATE:
        handleGuildEmojisUpdate(data);
        break;
    case GatewayEvent::GUILD_STICKERS_UPDATE:
        handleGuildStickersUpdate(data);
        break;
    case GatewayEvent::PRESENCE_UPDATE:
        handlePresenceUpdate(data);
        break;
    case GatewayEvent::WEBHOOKS_UPDATE:
        handleWebhooksUpdate(data);
        break;
    case GatewayEvent::INVITE_CREATE:
        handleInviteCreate(data);
        break;
    case GatewayEvent::INVITE_DELETE:
        handleInviteDelete(data);
        break;
    case GatewayEvent::STAGE_INSTANCE_CREATE:
        handleStageInstanceCreate(data);
        break;
    case GatewayEvent::STAGE_INSTANCE_UPDATE:
        handleStageInstanceUpdate(data);
        break;
    case GatewayEvent::STAGE_INSTANCE_DELETE:
        handleStageInstanceDelete(data);
        break;
    case GatewayEvent::GUILD_SCHEDULED_EVENT_CREATE:
        handleScheduledEventCreate(data);
        break;
    case GatewayEvent::GUILD_SCHEDULED_EVENT_UPDATE:
        handleScheduledEventUpdate(data);
        break;
    case GatewayEvent::GUILD_SCHEDULED_EVENT_DELETE:
        handleScheduledEventDelete(data);
        break;
    case GatewayEvent::INTEGRATION_CREATE:
        handleIntegrationCreate(data);
        break;
    case GatewayEvent::INTEGRATION_UPDATE:
        handleIntegrationUpdate(data);
        break;
    case GatewayEvent::INTEGRATION_DELETE:
        handleIntegrationDelete(data);
        break;
    case GatewayEvent::CHANNEL_PINS_UPDATE:
        handleChannelPinsUpdate(data);
        break;
    case GatewayEvent::RESUMED:
        handleResumed();
        break;
    case GatewayEvent::UNKNOWN:
        qCInfo(LogDiscord) << "Unknown gateway event: " << t;
        break;
    default:
        qCInfo(LogDiscord) << "Parsed but unhandled gateway event: " << t;
    }
}

void Gateway::handleReady(const Inbound &data)
{
    qCDebug(LogDiscord) << "Received ready event";

    Ready msg = data.getData<Ready>();

    {
        std::lock_guard lock(sessionMutex);
        if (msg.sessionId.hasValue())
            sessionId = msg.sessionId.get();
        if (msg.resumeGatewayUrl.hasValue())
            resumeGatewayUrl = msg.resumeGatewayUrl.get();
        canResume = !sessionId.isEmpty();
    }
    // Only a successful READY proves the connection is healthy — resetting
    // attempts here (and not in handleHello) ensures reconnectAttempts can
    // reach maxReconnectAttempts for connections that die before READY.
    reconnectAttempts = 0;

    emit gatewayReady(msg);
}

void Gateway::handleResumed()
{
    qCInfo(LogDiscord) << "Session resumed successfully";
    reconnectAttempts = 0;
    emit gatewayResumed();
}

void Gateway::handleReadySupplemental(const Inbound &data)
{
    qCDebug(LogDiscord) << "Received ready supplemental event";

    ReadySupplemental msg = data.getData<ReadySupplemental>();

    emit gatewayReadySupplemental(msg);
}

void Gateway::handleMessageCreate(const Inbound &data)
{
    // presentKeys is consumed only for MESSAGE_UPDATE messages (applyUpdate /
    // MessageManager::onMessageUpdated), so skip building it for the create path.
    Message msg = Message::fromJson(data.data.toObject(), false);

    emit gatewayMessageCreate(msg);
}

void Gateway::handleMessageUpdate(const Inbound &data)
{
    Message msg = data.getData<Message>();

    emit gatewayMessageUpdate(msg);
}

void Gateway::handleMessageDelete(const Inbound &data)
{
    MessageDelete event = data.getData<MessageDelete>();

    emit gatewayMessageDelete(event);
}

void Gateway::handleMessageDeleteBulk(const Inbound &data)
{
    MessageDeleteBulk event = data.getData<MessageDeleteBulk>();

    emit gatewayMessageDeleteBulk(event);
}

void Gateway::handleTypingStart(const Inbound &data)
{
    TypingStart event = data.getData<TypingStart>();

    emit gatewayTypingStart(event);
}

void Gateway::handleChannelCreate(const Inbound &data)
{
    ChannelCreate event = data.getData<ChannelCreate>();

    emit gatewayChannelCreate(event);
}

void Gateway::handleChannelUpdate(const Inbound &data)
{
    ChannelUpdate event = data.getData<ChannelUpdate>();

    emit gatewayChannelUpdate(event);
}

void Gateway::handleChannelDelete(const Inbound &data)
{
    ChannelDelete event = data.getData<ChannelDelete>();

    emit gatewayChannelDelete(event);
}

void Gateway::handleThreadCreate(const Inbound &data)
{
    ChannelCreate event = data.getData<ChannelCreate>();

    emit gatewayThreadCreate(event);
}

void Gateway::handleThreadUpdate(const Inbound &data)
{
    ChannelUpdate event = data.getData<ChannelUpdate>();

    emit gatewayThreadUpdate(event);
}

void Gateway::handleThreadDelete(const Inbound &data)
{
    ThreadDelete event = data.getData<ThreadDelete>();

    emit gatewayThreadDelete(event);
}

void Gateway::handleThreadListSync(const Inbound &data)
{
    ThreadListSync event = data.getData<ThreadListSync>();

    emit gatewayThreadListSync(event);
}

void Gateway::handleThreadMemberUpdate(const Inbound &data)
{
    ThreadMemberUpdate event = data.getData<ThreadMemberUpdate>();

    emit gatewayThreadMemberUpdate(event);
}

void Gateway::handleThreadMembersUpdate(const Inbound &data)
{
    ThreadMembersUpdate event = data.getData<ThreadMembersUpdate>();

    emit gatewayThreadMembersUpdate(event);
}

void Gateway::handleForumUnreads(const Inbound &data)
{
    ForumUnreads event = data.getData<ForumUnreads>();

    emit gatewayForumUnreads(event);
}

void Gateway::requestForumUnreads(Core::Snowflake guildId, Core::Snowflake forumId,
                                  const QList<QPair<Core::Snowflake, Core::Snowflake>> &threads)
{
    RequestForumUnreads request;
    request.guildId = guildId;
    request.channelId = forumId;
    request.threads = threads;

    sendPayload(request.toJson());
}

void Gateway::handleGuildCreate(const Inbound &data)
{
    GatewayGuild guild = data.getData<GatewayGuild>();

    emit gatewayGuildCreate(guild);
}

void Gateway::handleGuildMembersChunk(const Inbound &data)
{
    GuildMembersChunk chunk = data.getData<GuildMembersChunk>();

    emit gatewayGuildMembersChunk(chunk);
}

void Gateway::handleGuildMemberUpdate(const Inbound &data)
{
    GuildMemberUpdate event = data.getData<GuildMemberUpdate>();

    emit gatewayGuildMemberUpdate(event);
}

void Gateway::handleGuildMemberAdd(const Inbound &data)
{
    GuildMemberUpdate event = data.getData<GuildMemberUpdate>();

    emit gatewayGuildMemberAdd(event);
}

void Gateway::handleGuildMemberRemove(const Inbound &data)
{
    GuildMemberRemove event = data.getData<GuildMemberRemove>();

    emit gatewayGuildMemberRemove(event);
}

void Gateway::handleUserUpdate(const Inbound &data)
{
    User user = data.getData<User>();

    emit gatewayUserUpdate(user);
}

void Gateway::handleGuildRoleCreate(const Inbound &data)
{
    GuildRoleCreate event = data.getData<GuildRoleCreate>();

    emit gatewayGuildRoleCreate(event);
}

void Gateway::handleGuildRoleUpdate(const Inbound &data)
{
    GuildRoleUpdate event = data.getData<GuildRoleUpdate>();

    emit gatewayGuildRoleUpdate(event);
}

void Gateway::handleGuildRoleDelete(const Inbound &data)
{
    GuildRoleDelete event = data.getData<GuildRoleDelete>();

    emit gatewayGuildRoleDelete(event);
}

void Gateway::handleGuildDelete(const Inbound &data)
{
    GuildDelete event = data.getData<GuildDelete>();

    emit gatewayGuildDelete(event);
}

void Gateway::handleMessageAck(const Inbound &data)
{
    MessageAck event = data.getData<MessageAck>();

    emit gatewayMessageAck(event);
}

void Gateway::handleMessageReactionAdd(const Inbound &data)
{
    MessageReactionAdd event = data.getData<MessageReactionAdd>();

    emit gatewayMessageReactionAdd(event);
}

void Gateway::handleMessageReactionAddMany(const Inbound &data)
{
    MessageReactionAddMany event = data.getData<MessageReactionAddMany>();

    emit gatewayMessageReactionAddMany(event);
}

void Gateway::handleMessageReactionRemove(const Inbound &data)
{
    MessageReactionRemove event = data.getData<MessageReactionRemove>();

    emit gatewayMessageReactionRemove(event);
}

void Gateway::handleMessageReactionRemoveAll(const Inbound &data)
{
    MessageReactionRemoveAll event = data.getData<MessageReactionRemoveAll>();

    emit gatewayMessageReactionRemoveAll(event);
}

void Gateway::handleMessageReactionRemoveEmoji(const Inbound &data)
{
    MessageReactionRemoveEmoji event = data.getData<MessageReactionRemoveEmoji>();

    emit gatewayMessageReactionRemoveEmoji(event);
}

void Gateway::handleUserGuildSettingsUpdate(const Inbound &data)
{
    UserGuildSettings settings = data.getData<UserGuildSettings>();

    emit gatewayUserGuildSettingsUpdate(settings);
}

void Gateway::handleGuildMemberListUpdate(const Inbound &data)
{
    GuildMemberListUpdate update = data.getData<GuildMemberListUpdate>();

    emit gatewayGuildMemberListUpdate(update);
}

void Gateway::handleVoiceStateUpdate(const Inbound &data)
{
    VoiceState event = data.getData<VoiceState>();

    emit gatewayVoiceStateUpdate(event);
}

void Gateway::handleVoiceStateUpdateBatch(const Inbound &data)
{
    VoiceStateUpdateBatch batch = data.getData<VoiceStateUpdateBatch>();
    if (!batch.voiceStates.hasValue())
        return;

    for (const VoiceState &state : batch.voiceStates.get())
        emit gatewayVoiceStateUpdate(state);
}

void Gateway::handleVoiceServerUpdate(const Inbound &data)
{
    VoiceServerUpdate event = data.getData<VoiceServerUpdate>();

    emit gatewayVoiceServerUpdate(event);
}

void Gateway::handleRelationshipAdd(const Inbound &data)
{
    Relationship event = data.getData<Relationship>();
    emit gatewayRelationshipAdd(event);
}

void Gateway::handleRelationshipUpdate(const Inbound &data)
{
    RelationshipPartial event = data.getData<RelationshipPartial>();
    emit gatewayRelationshipUpdate(event);
}

void Gateway::handleRelationshipRemove(const Inbound &data)
{
    RelationshipPartial event = data.getData<RelationshipPartial>();
    emit gatewayRelationshipRemove(event);
}

void Gateway::handleUserNoteUpdate(const Inbound &data)
{
    UserNoteUpdate event = data.getData<UserNoteUpdate>();
    emit gatewayUserNoteUpdate(event);
}

void Gateway::requestGuildMembers(Core::Snowflake guildId, const QList<Core::Snowflake> &userIds)
{
    RequestGuildMembers request;
    request.guildId = guildId;
    request.userIds = userIds;
    request.presences = false;

    sendPayload(request.toJson());
}

void Gateway::sendVoiceStateUpdate(Core::Snowflake guildId, Core::Snowflake channelId, bool selfMute, bool selfDeaf)
{
    UpdateVoiceState msg;
    msg.guildId = guildId;
    if (channelId.isValid())
        msg.channelId = channelId;
    else
        msg.channelId = nullptr;
    msg.selfMute = selfMute;
    msg.selfDeaf = selfDeaf;

    sendPayload(msg.toJson());
}

void Gateway::sendPresenceUpdate(const QString &status)
{
    PresenceUpdateOutbound msg;
    msg.status = status;
    msg.since = 0;
    msg.afk = false;

    sendPayload(msg.toJson());
}

QString Gateway::gatewaySessionId() const
{
    std::lock_guard lock(sessionMutex);
    return sessionId;
}

void Gateway::handleHello(const Inbound &data)
{
    qCDebug(LogDiscord) << "Received hello";

    Hello msg = data.getData<Hello>();

    heartbeatInterval = msg.heartbeatInterval;
    heartbeatAckReceived = true;

    if (isResuming && canResume)
        resume();
    else
        identify();

    isResuming = false;

    if (msg.heartbeatInterval <= 0) {
        // A malformed HELLO must not strand the caller: the early return below
        // used to skip emitting gatewayHello(), so anything waiting on it
        // (session bootstrap) hung forever. Fall back to Discord's documented
        // default interval and continue as normal.
        qCWarning(LogDiscord) << "Invalid heartbeat interval in hello:" << msg.heartbeatInterval
                              << "- falling back to" << kDefaultHeartbeatInterval;
        heartbeatInterval = kDefaultHeartbeatInterval;
    }


    {
        std::lock_guard lock(heartbeatThreadMutex);
        if (!heartbeatThread.joinable()) {
            heartbeatStarted = false;
            heartbeatThread = std::thread(&Gateway::heartbeatLoop, this);
        }
    }

    // Wait (with timeout, woken by the heartbeat thread itself) for the
    // heartbeat loop to confirm it's running before notifying client code.
    // A condition variable avoids the previous 1s busy-wait on this thread,
    // which is effectively the UI thread via the queued payloadReceived signal.
    {
        std::unique_lock lock(heartbeatStartMutex);
        heartbeatStartCv.wait_for(lock, std::chrono::seconds(1),
                                  [this] { return heartbeatStarted.load(); });
    }

    emit gatewayHello();
}

void Gateway::identify()
{
    ClientPropertiesBuildParams params;
    params.clientAppState = "focused";
    params.includeClientHeartbeatSessionId = false;
    params.isFastConnect = false;
    params.gatewayConnectReasons = "AppSkeleton";
    ClientProperties properties = identity.buildClientProperties(params);

    UpdatePresence presence;
    presence.status = "unknown";
    presence.since = 0;
    presence.afk = false;

    ClientState clientState;

    Identify identify;
    identify.token = token;
    identify.capabilities = CURRENT_CAPABILITIES;
    identify.compress = false;
    identify.properties = properties;
    identify.presence = presence;
    identify.clientState = clientState;

    sendPayload(identify.toJson());
}

static int curlDebug(CURL *, curl_infotype type, char *data, size_t size, void *)
{
    if (type == CURLINFO_TEXT || type == CURLINFO_SSL_DATA_IN || type == CURLINFO_SSL_DATA_OUT) {
        qDebug().noquote() << QByteArray(data, size);
    }
    return 0;
}

void Gateway::networkLoop()
{
    do {
        shouldReconnect = false;

        // Choose URL: use resumeGatewayUrl if resuming, else gatewayUrl.
        // resumeGatewayUrl from READY is a bare host (e.g. wss://gateway-us-east1-b.discord.gg)
        // without query parameters — append them from the original gatewayUrl.
        QString connectUrl = gatewayUrl;
        {
            std::lock_guard lock(sessionMutex);
            if (isResuming && canResume && !resumeGatewayUrl.isEmpty()) {
                QUrl resumeUrl(resumeGatewayUrl);
                QUrl originalUrl(gatewayUrl);
                resumeUrl.setQuery(originalUrl.query());
                connectUrl = resumeUrl.toString();
            }
        }

        // Use a local handle for the connect phase so a concurrent UI-thread
        // send (which checks the shared `curl` under curlMutex) never touches a
        // half-initialized handle. Publish it only once the connection is up.
        CURL *connectingCurl = curl_easy_init();
        if (!connectingCurl) {
            qCCritical(LogDiscord) << "Failed to initialize curl";
            running = false;
            if (ingest)
                ingest->stop();
            emit disconnected(CloseCode::INTERNAL,
                              QStringLiteral("Failed to initialize gateway connection"));
            return;
        }

        curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
        qCDebug(LogDiscord) << "SSL backend:" << info->ssl_version;

        curl_easy_setopt(connectingCurl, CURLOPT_URL, connectUrl.toUtf8().constData());
        curl_easy_setopt(connectingCurl, CURLOPT_CONNECT_ONLY, 2L);
        curl_easy_setopt(connectingCurl, CURLOPT_CONNECTTIMEOUT, 15L);
        CurlUtils::applyCommonOptions(connectingCurl);

        CURLcode res = curl_easy_perform(connectingCurl);
        if (res != CURLE_OK) {
            qWarning() << "Failed to connect to gateway:" << curl_easy_strerror(res);

            // Retry connect failures (initial or reconnect) with capped backoff
            // and no attempt ceiling: Discord expects clients to keep retrying,
            // and only a fatal close code or an explicit stop() should end the
            // gateway. A transient network blip must not strand the client.
            curl_easy_cleanup(connectingCurl);

            // Snapshot after incrementing: the UI thread resets
            // reconnectAttempts on READY, and reading it again later could
            // observe 0 and compute an invalid backoff.
            const int attempt = reconnectAttempts.fetch_add(1) + 1;
            int delay = reconnectBackoffMs(attempt);
            const int overrideMs = nextReconnectDelayMs.exchange(0);
            if (overrideMs > 0)
                delay = std::max(delay, overrideMs);
            qCInfo(LogDiscord) << "Connect attempt" << attempt
                               << "failed, retrying in" << delay << "ms";
            emit reconnecting(attempt, maxReconnectAttempts);
            if (!waitInterruptible(std::chrono::milliseconds(delay)))
                break;
            shouldReconnect = true;
            continue;
        }

        {
            std::lock_guard lock(curlMutex);
            curl = connectingCurl;
        }
        emit connected();

        char chunk[8192];
        size_t rlen = 0;
        const curl_ws_frame *meta = nullptr;

        bool closeSent = false;
        while (running) {
            if (shouldReconnect)
                break;

            {
                std::lock_guard lock(curlMutex);

                if (wantToClose) {
                    if (!closeSent) {
                        closeSent = true;
                        uint8_t close_payload[2] = { 0x03, 0xE8 };
                        size_t bytesSent = 0;
                        curl_ws_send(curl, close_payload, sizeof(close_payload), &bytesSent, 0,
                                     CURLWS_CLOSE);
                    }

                    auto now = std::chrono::steady_clock::now();
                    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                     now.time_since_epoch())
                                     .count();
                    if (nowMs - closeTimeMs > closeTimeout.count()) {
                        running = false;
                        qCWarning(LogDiscord) << "Gateway close timeout, forcing disconnect";
                        emit disconnected(CloseCode::INTERNAL,
                                          QStringLiteral("Gateway close timed out"));
                        break;
                    }
                }

                rlen = 0;
                meta = nullptr;
                res = curl_ws_recv(curl, chunk, sizeof(chunk), &rlen, &meta);
            }

            if (res == CURLE_AGAIN) {
                CurlUtils::wsRecvWait(curl, curlMutex);

                if (shouldReconnect)
                    break;

                continue;
            }

            // GOT_NOTHING means the peer closed without a clean WS close frame
            // (recv returned 0 bytes); treat it as a dead connection and
            // reconnect rather than busy-looping forever in a silently-dead
            // "connected" state.
            if (res == CURLE_GOT_NOTHING) {
                qCWarning(LogDiscord) << "Gateway connection returned no data; reconnecting";
                shouldReconnect = true;
                break;
            }

            if (!meta) {
                // Any other error (e.g. CURLE_RECV_ERROR after the TCP socket
                // drops) leaves meta == nullptr. Treat it as a dead connection:
                // break out and reconnect instead of busy-looping at 100% CPU.
                qCWarning(LogDiscord) << "Gateway recv error" << res
                                      << "- treating as connection failure";
                if (!wantToClose)
                    shouldReconnect = true;
                break;
            }

            if (meta->flags & CURLWS_CLOSE) {
                int closeCode = 1000;
                QString closeReason;

                if (rlen >= 2) {
                    closeCode = (uint8_t(chunk[0]) << 8) | uint8_t(chunk[1]);
                    if (rlen > 2)
                        closeReason = QString::fromUtf8(chunk + 2, rlen - 2);
                }

                qCInfo(LogDiscord) << "Connection closed with code" << closeCode
                                   << "reason:" << closeReason;
                CloseCode cc = static_cast<CloseCode>(closeCode);
                emit disconnected(cc, closeReason);
                // 4008 (RATE_LIMITED) carries {"retry_after": ms} in the close
                // payload — honor it so a reconnect storm (e.g. session
                // replacement while another client is open) doesn't ping-pong
                // against the rate limiter.
                if (cc == CloseCode::RATE_LIMITED) {
                    QJsonParseError parseError;
                    const QJsonDocument doc =
                            QJsonDocument::fromJson(closeReason.toUtf8(), &parseError);
                    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                        const double retryAfterMs = doc.object().value("retry_after").toDouble();
                        if (retryAfterMs > 0)
                            nextReconnectDelayMs = qMin(static_cast<int>(retryAfterMs), 120'000);
                    }
                } else if (cc == CloseCode::TOO_MANY_SESSIONS) {
                    // The account is at its concurrent-session cap (another
                    // Discord client is open). Retry slowly so the user has
                    // time to close one — hot-looping here just gets us kicked
                    // again and again.
                    qCWarning(LogDiscord)
                            << "Too many concurrent sessions — close another Discord client "
                               "to free a slot; retrying slowly";
                    nextReconnectDelayMs = 60'000;
                }
                // 4006 (SESSION_NO_LONGER_VALID) and 4007 (INVALID_SEQ) mean
                // the resume session is dead — clear it so the reconnect does a
                // fresh IDENTIFY instead of looping RESUME → 4006 forever.
                if (cc == CloseCode::SESSION_NO_LONGER_VALID ||
                    cc == CloseCode::INVALID_SEQ) {
                    canResume = false;
                    std::lock_guard lock(sessionMutex);
                    sessionId.clear();
                }
                // Reconnect even before READY (canResume false): a network blip
                // while identifying must not permanently kill the gateway.
                if (!wantToClose && !isFatalCloseCode(cc))
                    shouldReconnect = true;
                break;
            }

            if (meta->flags & (CURLWS_PING | CURLWS_PONG))
                continue;

            ingest->push(QByteArray(chunk, rlen));
        }

        // Clean up current connection
        {
            std::lock_guard lock(curlMutex);
            curl_easy_cleanup(curl);
            curl = nullptr;
        }

        // If shouldReconnect was set (by RECONNECT/INVALID_SESSION opcode
        // handlers, zombie detection, a send failure, or a server close),
        // prepare for reconnection. There is no attempt ceiling: keep retrying
        // with capped exponential backoff until the session is restored, a
        // fatal close code stops us, or the gateway is explicitly stopped.
        if (shouldReconnect && running) {
            reconnectAttempts++;
            isResuming.store(canResume.load());

            // Join the heartbeat thread if it exited (e.g. zombie detection broke the loop)
            // so handleHello can start a fresh one on reconnect
            {
                std::lock_guard lock(heartbeatThreadMutex);
                if (heartbeatThread.joinable()) {
                    heartbeatCv.notify_all();
                    heartbeatThread.join();
                }
            }

            // Reset the IngestThread's zlib stream — the new connection starts a fresh
            // zlib context, so the old stream state would corrupt decompression
            ingest->reset();

            const int attempt = reconnectAttempts.load();
            int delay = reconnectBackoffMs(attempt);
            const int overrideMs = nextReconnectDelayMs.exchange(0);
            if (overrideMs > 0)
                delay = std::max(delay, overrideMs);
            qCInfo(LogDiscord) << "Reconnecting in" << delay << "ms (attempt" << attempt << ")";
            emit reconnecting(attempt, maxReconnectAttempts);
            if (!waitInterruptible(std::chrono::milliseconds(delay))) {
                shouldReconnect = false;
                break;
            }
        }

    } while (shouldReconnect && running);

    // Ensure heartbeat thread exits when the network loop is done
    running = false;
    heartbeatCv.notify_all();
    {
        std::lock_guard lock(heartbeatThreadMutex);
        if (heartbeatThread.joinable())
            heartbeatThread.join();
    }

    if (ingest)
        ingest->stop();
}

void Gateway::heartbeatLoop()
{
    {
        std::lock_guard lock(heartbeatStartMutex);
        heartbeatStarted = true;
    }
    heartbeatStartCv.notify_all();
    qCDebug(LogDiscord) << "Heartbeat loop started, interval:" << heartbeatInterval;

    while (running) {
        if (!heartbeatAckReceived) {
            int missed = ++missedHeartbeatAcks;
            if (missed >= maxMissedHeartbeatAcks) {
                qCWarning(LogDiscord) << "No heartbeat ACK after" << missed
                                      << "heartbeats — zombie connection detected";
                shouldReconnect = true;
                break;
            }
            qCWarning(LogDiscord) << "Missed heartbeat ACK (" << missed << "of"
                                  << maxMissedHeartbeatAcks << ") — tolerating";
        } else {
            missedHeartbeatAcks = 0;
        }
        heartbeatAckReceived = false;

        QoSHeartbeat heartbeat;
        heartbeat.seq = lastReceivedSequence;
        heartbeat.qos->ver = 27;
        heartbeat.qos->active = true;
        heartbeat.qos->reasons = { "foregrounded" };

        sendPayload(heartbeat.toJson());

        {
            std::unique_lock lock(heartbeatMutex);
            bool stop = heartbeatCv.wait_for(lock, std::chrono::milliseconds(heartbeatInterval),
                                             [this] { return !running || shouldReconnect.load(); });

            if (stop)
                break;
        }
    }
}

#ifndef QT_NO_DEBUG
void Gateway::debugForceReconnect()
{
    qCInfo(LogDiscord) << "DEBUG: Forcing reconnect (simulating op 7 RECONNECT)";
    shouldReconnect = true;
}
#endif

void Gateway::resume()
{
    qCInfo(LogDiscord) << "Sending RESUME";
    Resume resumeMsg;
    resumeMsg.token = token;
    {
        std::lock_guard lock(sessionMutex);
        resumeMsg.sessionId = sessionId;
    }
    resumeMsg.seq = lastReceivedSequence.load();
    sendPayload(resumeMsg.toJson());
}

void Gateway::handleGuildUpdate(const Inbound &data)
{
    GatewayGuild guild = data.getData<GatewayGuild>();
    emit gatewayGuildUpdate(guild);
}

void Gateway::handleGuildBanAdd(const Inbound &data)
{
    GuildBan event = data.getData<GuildBan>();
    emit gatewayGuildBanAdd(event);
}

void Gateway::handleGuildBanRemove(const Inbound &data)
{
    GuildBan event = data.getData<GuildBan>();
    emit gatewayGuildBanRemove(event);
}

void Gateway::handleGuildEmojisUpdate(const Inbound &data)
{
    GuildEmojisUpdate event = data.getData<GuildEmojisUpdate>();
    emit gatewayGuildEmojisUpdate(event);
}

void Gateway::handleGuildStickersUpdate(const Inbound &data)
{
    GuildStickersUpdate event = data.getData<GuildStickersUpdate>();
    emit gatewayGuildStickersUpdate(event);
}

void Gateway::handlePresenceUpdate(const Inbound &data)
{
    PresenceUpdate event = data.getData<PresenceUpdate>();
    emit gatewayPresenceUpdate(event);
}

void Gateway::handleWebhooksUpdate(const Inbound &data)
{
    WebhooksUpdate event = data.getData<WebhooksUpdate>();
    emit gatewayWebhooksUpdate(event);
}

void Gateway::handleInviteCreate(const Inbound &data)
{
    InviteCreate event = data.getData<InviteCreate>();
    emit gatewayInviteCreate(event);
}

void Gateway::handleInviteDelete(const Inbound &data)
{
    InviteDelete event = data.getData<InviteDelete>();
    emit gatewayInviteDelete(event);
}

void Gateway::handleStageInstanceCreate(const Inbound &data)
{
    StageInstance instance = data.getData<StageInstance>();
    emit gatewayStageInstanceCreate(instance);
}

void Gateway::handleStageInstanceUpdate(const Inbound &data)
{
    StageInstance instance = data.getData<StageInstance>();
    emit gatewayStageInstanceUpdate(instance);
}

void Gateway::handleStageInstanceDelete(const Inbound &data)
{
    StageInstance instance = data.getData<StageInstance>();
    emit gatewayStageInstanceDelete(instance);
}

void Gateway::handleScheduledEventCreate(const Inbound &data)
{
    GuildScheduledEvent event = data.getData<GuildScheduledEvent>();
    emit gatewayScheduledEventCreate(event);
}

void Gateway::handleScheduledEventUpdate(const Inbound &data)
{
    GuildScheduledEvent event = data.getData<GuildScheduledEvent>();
    emit gatewayScheduledEventUpdate(event);
}

void Gateway::handleScheduledEventDelete(const Inbound &data)
{
    GuildScheduledEvent event = data.getData<GuildScheduledEvent>();
    emit gatewayScheduledEventDelete(event);
}

void Gateway::handleIntegrationCreate(const Inbound &data)
{
    IntegrationCreate event = data.getData<IntegrationCreate>();
    emit gatewayIntegrationCreate(event);
}

void Gateway::handleIntegrationUpdate(const Inbound &data)
{
    IntegrationUpdate event = data.getData<IntegrationUpdate>();
    emit gatewayIntegrationUpdate(event);
}

void Gateway::handleIntegrationDelete(const Inbound &data)
{
    IntegrationDelete event = data.getData<IntegrationDelete>();
    emit gatewayIntegrationDelete(event);
}

void Gateway::handleChannelPinsUpdate(const Inbound &data)
{
    ChannelPinsUpdate event = data.getData<ChannelPinsUpdate>();
    emit gatewayChannelPinsUpdate(event);
}

int Gateway::reconnectBackoffMs(int attempt) const
{
    // Exponential backoff: 1s, 2s, 4s, ... capped at 30s, plus up to 1s jitter
    // to avoid thundering-herd reconnects.
    int base = std::min(1000 << (attempt - 1), 30000);
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> jitter(0, 999);
    return base + jitter(rng);
}

bool Gateway::waitInterruptible(std::chrono::milliseconds delay) const
{
    // Sleep in small slices so hardStop() (which clears `running`) and stop()
    // (which sets `wantToClose`) are never blocked by a long backoff wait.
    const auto deadline = std::chrono::steady_clock::now() + delay;
    while (running && !wantToClose) {
        const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds(0))
            return true;
        std::this_thread::sleep_for(std::min(remaining, std::chrono::milliseconds(50)));
    }
    return false;
}

bool Gateway::isFatalCloseCode(CloseCode code) const
{
    switch (code) {
    case CloseCode::AUTHENTICATION_FAILED:
    case CloseCode::INVALID_SHARD:
    case CloseCode::SHARDING_REQUIRED:
    case CloseCode::INVALID_API_VERSION:
    case CloseCode::INVALID_INTENTS:
    case CloseCode::DISALLOWED_INTENTS:
        return true;
    default:
        return false;
    }
}

} // namespace Discord
} // namespace Acheron

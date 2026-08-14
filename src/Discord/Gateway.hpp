#pragma once

#include <QObject>
#include <QString>

#include <curl/curl.h>

#include <memory>

#include "Enums.hpp"
#include "IngestThread.hpp"
#include "Inbound.hpp"
#include "Events.hpp"

namespace Acheron {

namespace Proto {
struct PreloadedUserSettings;
}

namespace Discord {

class ClientIdentity;

class Gateway : public QObject
{
    Q_OBJECT
public:
    explicit Gateway(const QString &token, const QString &gatewayUrl, ClientIdentity &identity,
                     QObject *parent = nullptr);
    ~Gateway() override;

    void start();
    void stop();
    void hardStop();

    void subscribeToGuild(Core::Snowflake guildId, Core::Snowflake channelId, const QList<QPair<int, int>> &ranges);
    void requestGuildMembers(Core::Snowflake guildId, const QList<Core::Snowflake> &userIds);
    void requestForumUnreads(Core::Snowflake guildId, Core::Snowflake forumId,
                             const QList<QPair<Core::Snowflake, Core::Snowflake>> &threads);
    void sendVoiceStateUpdate(Core::Snowflake guildId, Core::Snowflake channelId, bool selfMute, bool selfDeaf);

#ifndef QT_NO_DEBUG
    // Debug: simulate a server RECONNECT opcode
    void debugForceReconnect();
#endif

signals:
    void connected();
    void disconnected(CloseCode code, const QString &reason);
    void reconnecting(int attempt, int maxAttempts);

    void gatewayHello();
    void gatewayReady(const Ready &data);
    void gatewayReadySupplemental(const ReadySupplemental &data);
    void gatewayMessageCreate(const Message &data);
    void gatewayMessageUpdate(const Message &data);
    void gatewayMessageDelete(const MessageDelete &data);
    void gatewayMessageDeleteBulk(const MessageDeleteBulk &data);
    void gatewayTypingStart(const TypingStart &data);
    void gatewayChannelCreate(const ChannelCreate &data);
    void gatewayChannelUpdate(const ChannelUpdate &data);
    void gatewayChannelDelete(const ChannelDelete &data);
    void gatewayThreadCreate(const ChannelCreate &data);
    void gatewayThreadUpdate(const ChannelUpdate &data);
    void gatewayThreadDelete(const ThreadDelete &data);
    void gatewayThreadListSync(const ThreadListSync &data);
    void gatewayThreadMemberUpdate(const ThreadMemberUpdate &data);
    void gatewayThreadMembersUpdate(const ThreadMembersUpdate &data);
    void gatewayForumUnreads(const ForumUnreads &data);
    void gatewayGuildCreate(const GatewayGuild &data);
    void gatewayGuildDelete(const GuildDelete &data);
    void gatewayGuildMembersChunk(const GuildMembersChunk &data);
    void gatewayGuildMemberUpdate(const GuildMemberUpdate &data);
    void gatewayGuildMemberAdd(const GuildMemberUpdate &data);
    void gatewayGuildMemberRemove(const GuildMemberRemove &data);
    void gatewayUserUpdate(const User &data);
    void gatewayGuildRoleCreate(const GuildRoleCreate &data);
    void gatewayGuildRoleUpdate(const GuildRoleUpdate &data);
    void gatewayGuildRoleDelete(const GuildRoleDelete &data);
    void gatewayMessageAck(const MessageAck &data);
    void gatewayMessageReactionAdd(const MessageReactionAdd &data);
    void gatewayMessageReactionAddMany(const MessageReactionAddMany &data);
    void gatewayMessageReactionRemove(const MessageReactionRemove &data);
    void gatewayMessageReactionRemoveAll(const MessageReactionRemoveAll &data);
    void gatewayMessageReactionRemoveEmoji(const MessageReactionRemoveEmoji &data);
    void gatewayUserGuildSettingsUpdate(const UserGuildSettings &data);
    void gatewayGuildMemberListUpdate(const GuildMemberListUpdate &data);
    void gatewayVoiceStateUpdate(const VoiceState &data);
    void gatewayVoiceServerUpdate(const VoiceServerUpdate &data);
    void gatewayRelationshipAdd(const Relationship &data);
    void gatewayRelationshipUpdate(const RelationshipPartial &data);
    void gatewayRelationshipRemove(const RelationshipPartial &data);
    void gatewayUserNoteUpdate(const UserNoteUpdate &data);

    // Guild events
    void gatewayGuildUpdate(const GatewayGuild &data);
    void gatewayGuildBanAdd(const GuildBan &data);
    void gatewayGuildBanRemove(const GuildBan &data);
    void gatewayGuildEmojisUpdate(const GuildEmojisUpdate &data);
    void gatewayGuildStickersUpdate(const GuildStickersUpdate &data);

    // Presence
    void gatewayPresenceUpdate(const PresenceUpdate &data);

    // Webhooks & Invites
    void gatewayWebhooksUpdate(const WebhooksUpdate &data);
    void gatewayInviteCreate(const InviteCreate &data);
    void gatewayInviteDelete(const InviteDelete &data);

    // Stage instances
    void gatewayStageInstanceCreate(const StageInstance &data);
    void gatewayStageInstanceUpdate(const StageInstance &data);
    void gatewayStageInstanceDelete(const StageInstance &data);

    // Scheduled events
    void gatewayScheduledEventCreate(const GuildScheduledEvent &data);
    void gatewayScheduledEventUpdate(const GuildScheduledEvent &data);
    void gatewayScheduledEventDelete(const GuildScheduledEvent &data);

    // Integrations
    void gatewayIntegrationCreate(const IntegrationCreate &data);
    void gatewayIntegrationUpdate(const IntegrationUpdate &data);
    void gatewayIntegrationDelete(const IntegrationDelete &data);

    // Channel pins
    void gatewayChannelPinsUpdate(const ChannelPinsUpdate &data);

private:
    void sendPayload(const QJsonObject &obj);
    void sendPayload(const QByteArray &data);

    // this function is called by the network thread
    void onPayloadReceived(const QJsonObject &root);
    void handleDispatch(const Inbound &data);
    void handleReady(const Inbound &data);
    void handleReadySupplemental(const Inbound &data);
    void handleMessageCreate(const Inbound &data);
    void handleMessageUpdate(const Inbound &data);
    void handleMessageDelete(const Inbound &data);
    void handleMessageDeleteBulk(const Inbound &data);
    void handleTypingStart(const Inbound &data);
    void handleChannelCreate(const Inbound &data);
    void handleChannelUpdate(const Inbound &data);
    void handleChannelDelete(const Inbound &data);
    void handleThreadCreate(const Inbound &data);
    void handleThreadUpdate(const Inbound &data);
    void handleThreadDelete(const Inbound &data);
    void handleThreadListSync(const Inbound &data);
    void handleThreadMemberUpdate(const Inbound &data);
    void handleThreadMembersUpdate(const Inbound &data);
    void handleForumUnreads(const Inbound &data);
    void handleGuildCreate(const Inbound &data);
    void handleGuildDelete(const Inbound &data);
    void handleGuildMembersChunk(const Inbound &data);
    void handleGuildMemberUpdate(const Inbound &data);
    void handleGuildMemberAdd(const Inbound &data);
    void handleGuildMemberRemove(const Inbound &data);
    void handleUserUpdate(const Inbound &data);
    void handleGuildRoleCreate(const Inbound &data);
    void handleGuildRoleUpdate(const Inbound &data);
    void handleGuildRoleDelete(const Inbound &data);
    void handleMessageAck(const Inbound &data);
    void handleMessageReactionAdd(const Inbound &data);
    void handleMessageReactionAddMany(const Inbound &data);
    void handleMessageReactionRemove(const Inbound &data);
    void handleMessageReactionRemoveAll(const Inbound &data);
    void handleMessageReactionRemoveEmoji(const Inbound &data);
    void handleUserGuildSettingsUpdate(const Inbound &data);
    void handleGuildMemberListUpdate(const Inbound &data);
    void handleVoiceStateUpdate(const Inbound &data);
    void handleVoiceStateUpdateBatch(const Inbound &data);
    void handleVoiceServerUpdate(const Inbound &data);
    void handleRelationshipAdd(const Inbound &data);
    void handleRelationshipUpdate(const Inbound &data);
    void handleRelationshipRemove(const Inbound &data);
    void handleUserNoteUpdate(const Inbound &data);
    void handleGuildUpdate(const Inbound &data);
    void handleGuildBanAdd(const Inbound &data);
    void handleGuildBanRemove(const Inbound &data);
    void handleGuildEmojisUpdate(const Inbound &data);
    void handleGuildStickersUpdate(const Inbound &data);
    void handlePresenceUpdate(const Inbound &data);
    void handleWebhooksUpdate(const Inbound &data);
    void handleInviteCreate(const Inbound &data);
    void handleInviteDelete(const Inbound &data);
    void handleStageInstanceCreate(const Inbound &data);
    void handleStageInstanceUpdate(const Inbound &data);
    void handleStageInstanceDelete(const Inbound &data);
    void handleScheduledEventCreate(const Inbound &data);
    void handleScheduledEventUpdate(const Inbound &data);
    void handleScheduledEventDelete(const Inbound &data);
    void handleIntegrationCreate(const Inbound &data);
    void handleIntegrationUpdate(const Inbound &data);
    void handleIntegrationDelete(const Inbound &data);
    void handleChannelPinsUpdate(const Inbound &data);
    void handleHello(const Inbound &data);
    void identify();
    void resume();
    bool isFatalCloseCode(CloseCode code) const;

    int reconnectBackoffMs(int attempt) const;
    bool waitInterruptible(std::chrono::milliseconds delay) const;

    void networkLoop();
    void heartbeatLoop();

private:
    QString token;
    QString gatewayUrl;
    ClientIdentity &identity;

    std::atomic<bool> running;

    std::mutex curlMutex;
    CURL *curl = nullptr;

    QByteArray receiveBuffer;
    IngestThread *ingest = nullptr;

    std::atomic<bool> wantToClose{ false };
    std::thread networkThread;
    std::atomic<int64_t> closeTimeMs{ 0 };
    static constexpr std::chrono::milliseconds closeTimeout = std::chrono::milliseconds(1000);

    std::atomic<int> lastReceivedSequence = 0;

    std::atomic<int> heartbeatInterval = 0;
    std::mutex heartbeatMutex;
    std::mutex heartbeatThreadMutex;
    std::condition_variable heartbeatCv;
    std::thread heartbeatThread;

    // sessionId and resumeGatewayUrl are written on the payload-processing
    // (UI) thread and read on the network thread — both are guarded by this mutex.
    mutable std::mutex sessionMutex;
    QString sessionId;
    QString resumeGatewayUrl;

    std::atomic<bool> heartbeatAckReceived{ true };
    std::atomic<bool> heartbeatStarted{ false };
    std::atomic<int> missedHeartbeatAcks{ 0 };
    static constexpr int maxMissedHeartbeatAcks = 2;
    std::mutex heartbeatStartMutex;
    std::condition_variable heartbeatStartCv;
    std::atomic<bool> shouldReconnect{ false };
    std::atomic<bool> canResume{ false };
    std::atomic<bool> isResuming{ false };
    std::atomic<int> reconnectAttempts{ 0 };
    static constexpr int maxReconnectAttempts = 5;
};

} // namespace Discord
} // namespace Acheron

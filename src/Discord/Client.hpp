#pragma once

#include <QObject>
#include <QNetworkReply>
#include <QHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>

#include <optional>

#include "Core/Result.hpp"
#include "Core/Snowflake.hpp"
#include "Core/Enums.hpp"
#include "Core/PendingAttachment.hpp"

#include "Proto/UserSettings.hpp"

#include "Gateway.hpp"
#include "HttpClient.hpp"
#include "ClientIdentity.hpp"
#include "Entities.hpp"

namespace Acheron {
namespace Discord {

using Core::Snowflake;

class Client : public QObject
{
    Q_OBJECT
public:
    enum class MessageLoadType {
        Latest,
        History,
        Future,
        Jump,
        Created,
        Updated,
    };
    Q_ENUM(MessageLoadType)

    explicit Client(const QString &token, const QString &gatewayUrl, const QString &baseUrl,
                    CaptchaResolver *captchaResolver = nullptr, QObject *parent = nullptr);
    ~Client() override;

    void start();
    void stop();

    [[nodiscard]] Core::ConnectionState getState() const;

    using MessagesCallback = std::function<void(const Core::Result<QList<Message>> &)>;
    void fetchLatestMessages(Core::Snowflake channelId, int limit, MessagesCallback callback);
    void fetchHistory(Core::Snowflake channelId, Core::Snowflake beforeId, int limit,
                      MessagesCallback callback);
    void fetchMessage(Core::Snowflake channelId, Core::Snowflake messageId,
                      MessagesCallback callback);

    using ApplicationCommandsCallback = std::function<void(const Core::Result<QList<ApplicationCommand>> &)>;
    // Fetches CHAT_INPUT application commands available in a channel (the search
    // endpoint the Discord client uses for slash-command autocomplete).
    void fetchApplicationCommands(Core::Snowflake channelId, const QString &query,
                                  ApplicationCommandsCallback callback);

    // Sends a chat-input interaction (slash command) to the semi-internal
    // /interactions endpoint. `options` are the parsed values matching the
    // command's option tree (see InteractionOptionValue).
    void sendApplicationCommandInteraction(Core::Snowflake channelId, Core::Snowflake guildId,
                                           const ApplicationCommand &command,
                                           const QList<InteractionOptionValue> &options,
                                           const QString &nonce = QString());

    using ProfileCallback = std::function<void(const Core::Result<UserProfile> &)>;
    void fetchUserProfile(Core::Snowflake userId, Core::Snowflake guildId, ProfileCallback callback);

    using DMChannelsCallback = std::function<void(const Core::Result<QList<Channel>> &)>;
    void fetchDMChannels(DMChannelsCallback callback);

    void setUserNote(Core::Snowflake userId, const QString &note);

    // Profile editing REST API
    using GenericCallback = std::function<void(const Core::Result<QJsonObject> &)>;
    void fetchOwnProfile(GenericCallback callback);
    void updateProfile(const QJsonObject &payload, GenericCallback callback);

    // Connections REST API
    void fetchConnections(GenericCallback callback);
    void removeConnection(const QString &type, const QString &id, GenericCallback callback);

    // Authorized apps REST API
    void fetchAuthorizedApps(GenericCallback callback);
    void revokeAuthorizedApp(Core::Snowflake appId, GenericCallback callback);

    // Privacy settings REST API
    void fetchPrivacySettings(GenericCallback callback);
    void updatePrivacySettings(const QJsonObject &payload, GenericCallback callback);

    // Custom status / presence REST API
    void updateCustomStatus(const QString &text, const QString &emojiName, qint64 expiresAt,
                            GenericCallback callback);
    void clearCustomStatus(GenericCallback callback);

    struct ForumThreadSearchResult
    {
        QList<Channel> threads;
        QHash<Snowflake, Message> firstMessages; // (thread id, starter message)
        bool hasMore = false;
        bool indexNotReady = false; // 202
        int retryAfterSeconds = 0;
    };
    using ForumThreadsCallback = std::function<void(const Core::Result<ForumThreadSearchResult> &)>;
    // "last_message_time" / "creation_time"
    void searchForumThreads(Snowflake forumId, int offset, const QString &sortBy, ForumThreadsCallback callback);

    struct CreatedForumThread
    {
        Channel thread;
        std::optional<Message> starterMessage;
    };
    using ForumThreadCallback = std::function<void(const Core::Result<CreatedForumThread> &)>;
    void createForumThread(Snowflake forumId, const QString &name,
                           const QList<Snowflake> &appliedTags, const QString &content,
                           const QString &nonce,
                           const QList<Core::PendingAttachment> &attachments,
                           ForumThreadCallback callback);

    using ForumPostDataCallback = std::function<void(const Core::Result<QHash<Snowflake, Message>> &)>;
    void fetchForumPostData(Snowflake forumId, const QList<Snowflake> &threadIds, ForumPostDataCallback callback);

    void joinThread(Snowflake threadId);
    void leaveThread(Snowflake threadId);

    struct ThreadListResult
    {
        QList<Channel> threads;
        QList<ThreadMember> members;
        bool hasMore = false;
        bool indexNotReady = false; // 202
        int retryAfterSeconds = 0;
    };
    using ThreadListCallback = std::function<void(const Core::Result<ThreadListResult> &)>;
    void searchThreads(Snowflake channelId, bool archived, int offset, ThreadListCallback callback);

    void sendMessage(Snowflake channelId, const QString &content, const QString &nonce,
                     Snowflake replyToMessageId = Snowflake::Invalid,
                     const QList<Core::PendingAttachment> &attachments = {});
    bool cancelMessageSend(const QString &nonce);
    void editMessage(Core::Snowflake channelId, Core::Snowflake messageId, const QString &content);
    void deleteMessage(Core::Snowflake channelId, Core::Snowflake messageId);
    void pinMessage(Core::Snowflake channelId, Core::Snowflake messageId);
    void unpinMessage(Core::Snowflake channelId, Core::Snowflake messageId,
                      std::function<void(bool success)> completion = nullptr);
    void getPinnedMessages(Core::Snowflake channelId,
                           const MessagesCallback &callback);

    void addReaction(Core::Snowflake channelId, Core::Snowflake messageId, const QString &emoji,
                     bool isBurst = false);
    void removeReaction(Core::Snowflake channelId, Core::Snowflake messageId, const QString &emoji,
                        bool isBurst = false);

    // Votes on a message poll. `pollId` is the message id in practice (the
    // poll entity on a message has no distinct id). The successful response is
    // the updated message, so the caller can refresh its poll results.
    using PollVoteCallback = std::function<void(const Core::Result<Message> &)>;
    void votePoll(Core::Snowflake channelId, Core::Snowflake messageId, Core::Snowflake pollId,
                  const QList<int> &answerIds, PollVoteCallback callback = {});

    struct AckEntry
    {
        Core::Snowflake channelId;
        Core::Snowflake messageId;
        int readStateType = 0;
    };

    void ackMessage(Core::Snowflake channelId, Core::Snowflake messageId, int flags, int lastViewed);
    void ackBulk(const QList<AckEntry> &entries);

    void sendVoiceStateUpdate(Core::Snowflake guildId, Core::Snowflake channelId, bool selfMute, bool selfDeaf);
    // Sets own presence status ("online", "idle", "dnd", "invisible", "offline").
    void setPresenceStatus(const QString &status);

    void leaveGuild(Snowflake guildId);

#ifndef QT_NO_DEBUG
    void debugForceReconnect();
#endif

    void subscribeToGuildChannel(Core::Snowflake guildId, Core::Snowflake channelId,
                                 const QList<QPair<int, int>> &ranges);
    void ensureSubscriptionByChannel(Snowflake channelId);
    void requestForumUnreads(Snowflake forumId, const QList<QPair<Snowflake, Snowflake>> &threads);
    void requestGuildMembers(Snowflake guildId, const QList<Snowflake> &userIds);

    // Sticker
    void sendSticker(Core::Snowflake channelId, Core::Snowflake stickerId);

    // Friend / relationship management
    void sendFriendRequest(const QString &username, const QString &tag);
    void acceptFriendRequest(Core::Snowflake userId);
    void removeFriend(Core::Snowflake userId);
    void blockUser(Core::Snowflake userId);

    // === Guild management REST API ===

    // Bans
    using BansCallback = std::function<void(const Core::Result<QList<BanEntry>> &)>;
    void fetchGuildBans(Core::Snowflake guildId, BansCallback callback);
    void unbanMember(Core::Snowflake guildId, Core::Snowflake userId);
    void banMember(Core::Snowflake guildId, Core::Snowflake userId,
                   int deleteMessageSeconds = 0, const QString &reason = QString());
    void kickMember(Core::Snowflake guildId, Core::Snowflake userId,
                    const QString &reason = QString());
    void tempBanMember(Core::Snowflake guildId, Core::Snowflake userId,
                       int deleteMessageSeconds, const QString &reason, int durationSeconds);
    void setMemberMute(Core::Snowflake guildId, Core::Snowflake userId, bool muted,
                       const QString &reason = QString());
    void setMemberDeaf(Core::Snowflake guildId, Core::Snowflake userId, bool deafened,
                       const QString &reason = QString());
    // Times out a member (communication_disabled_until). `durationSeconds <= 0`
    // removes the timeout.
    void setMemberTimeout(Core::Snowflake guildId, Core::Snowflake userId, int durationSeconds,
                          const QString &reason = QString());

    // Invites
    using InvitesCallback = std::function<void(const Core::Result<QList<InviteData>> &)>;
    void fetchGuildInvites(Core::Snowflake guildId, InvitesCallback callback);
    void revokeInvite(Core::Snowflake channelId, const QString &code);

    // Audit Log
    using AuditLogCallback = std::function<void(const Core::Result<AuditLogData> &)>;
    void fetchGuildAuditLog(Core::Snowflake guildId, Core::Snowflake userIdFilter,
                            Core::Snowflake actionTypeFilter, Core::Snowflake beforeId,
                            int limit, AuditLogCallback callback);

    // Webhooks
    using WebhooksCallback = std::function<void(const Core::Result<QList<WebhookData>> &)>;
    void fetchGuildWebhooks(Core::Snowflake guildId, WebhooksCallback callback);
    void createWebhook(Core::Snowflake channelId, const QString &name, const QString &avatar = QString());
    void deleteWebhook(Core::Snowflake webhookId);
    void modifyWebhook(Core::Snowflake webhookId, const QString &name, const QString &channelId);

    // Integrations
    using IntegrationsCallback = std::function<void(const Core::Result<QList<IntegrationData>> &)>;
    void fetchGuildIntegrations(Core::Snowflake guildId, IntegrationsCallback callback);
    void deleteIntegration(Core::Snowflake guildId, Core::Snowflake integrationId);

    // Roles
    void modifyRole(Core::Snowflake guildId, Core::Snowflake roleId, const Role &role);
    void createRole(Core::Snowflake guildId, const QString &name, Permissions permissions,
                    int color, bool hoist, bool mentionable);
    void deleteRole(Core::Snowflake guildId, Core::Snowflake roleId);
    void reorderRoles(Core::Snowflake guildId, const QList<QPair<Core::Snowflake, int>> &rolePositions);

    // Guild settings
    void modifyGuild(Core::Snowflake guildId, const QString &name);

    [[nodiscard]] Core::Snowflake getGuildIdForChannel(Core::Snowflake channelId) const;

    [[nodiscard]] PremiumTier getGuildPremiumTier(Core::Snowflake guildId) const;
    [[nodiscard]] qint64 getMaxUploadSize(Core::Snowflake channelId) const;

    [[nodiscard]] const Proto::PreloadedUserSettings &getSettings() const;
    [[nodiscard]] const User &getMe() const;
    [[nodiscard]] HttpClient *getHttpClient() const { return httpClient; }

signals:
    void stateChanged(Core::ConnectionState state);
    void ready(const Ready &data);
    void readySupplemental(const ReadySupplemental &data);
    void messageCreated(const Message &msg);
    void messageUpdated(const Message &msg);
    void messageDeleted(const MessageDelete &event);
    void messagesDeletedBulk(const MessageDeleteBulk &event);
    void typingStart(const TypingStart &event);
    void channelCreated(const ChannelCreate &event);
    void channelUpdated(const ChannelUpdate &event);
    void channelDeleted(const ChannelDelete &event);
    void threadCreated(const ChannelCreate &event);
    void threadUpdated(const ChannelUpdate &event);
    void threadDeleted(const ThreadDelete &event);
    void threadListSync(const ThreadListSync &event);
    void threadMemberUpdated(const ThreadMemberUpdate &event);
    void threadMembersUpdated(const ThreadMembersUpdate &event);
    void forumUnreads(const ForumUnreads &event);
    void guildCreated(const GatewayGuild &guild);
    void guildDeleted(const GuildDelete &event);
    void guildMembersChunk(const GuildMembersChunk &chunk);
    void guildMemberUpdated(const GuildMemberUpdate &event);
    void guildMemberAdded(const GuildMemberUpdate &event);
    void guildMemberRemoved(const GuildMemberRemove &event);
    void ownUserUpdated(const User &user);
    void guildRoleCreated(const GuildRoleCreate &event);
    void guildRoleUpdated(const GuildRoleUpdate &event);
    void guildRoleDeleted(const GuildRoleDelete &event);
    void messageAcked(const MessageAck &event);
    void messageReactionAdd(const MessageReactionAdd &event);
    void messageReactionAddMany(const MessageReactionAddMany &event);
    void messageReactionRemove(const MessageReactionRemove &event);
    void messageReactionRemoveAll(const MessageReactionRemoveAll &event);
    void messageReactionRemoveEmoji(const MessageReactionRemoveEmoji &event);
    void userGuildSettingsUpdated(const UserGuildSettings &settings);
    void guildMemberListUpdate(const GuildMemberListUpdate &event);
    void voiceStateUpdated(const VoiceState &event);
    void voiceServerUpdated(const VoiceServerUpdate &event);
    void relationshipAdded(const Relationship &event);
    void relationshipUpdated(const RelationshipPartial &event);
    void relationshipRemoved(const RelationshipPartial &event);
    void userNoteUpdated(const UserNoteUpdate &event);

    // New gateway events
    void guildUpdated(const GatewayGuild &event);
    void guildBanAdded(const GuildBan &event);
    void guildBanRemoved(const GuildBan &event);
    void guildEmojisUpdated(const GuildEmojisUpdate &event);
    void guildStickersUpdated(const GuildStickersUpdate &event);
    void presenceUpdated(const PresenceUpdate &event);
    void webhooksUpdated(const WebhooksUpdate &event);
    void inviteCreated(const InviteCreate &event);
    void inviteDeleted(const InviteDelete &event);
    void stageInstanceCreated(const StageInstance &event);
    void stageInstanceUpdated(const StageInstance &event);
    void stageInstanceDeleted(const StageInstance &event);
    void scheduledEventCreated(const GuildScheduledEvent &event);
    void scheduledEventUpdated(const GuildScheduledEvent &event);
    void scheduledEventDeleted(const GuildScheduledEvent &event);
    void integrationCreated(const IntegrationCreate &event);
    void integrationUpdated(const IntegrationUpdate &event);
    void integrationDeleted(const IntegrationDelete &event);
    void channelPinsUpdated(const ChannelPinsUpdate &event);

    void messageSendFailed(const QString &nonce, const QString &error);
    void guildLeaveFailed(Core::Snowflake guildId, const QString &error);
    void attachmentUploadProgress(const QString &nonce, int fileIndex, qint64 sent, qint64 total);

    // REST-result signals for guild settings UI
    void guildBansFetched(Core::Snowflake guildId, const QList<Discord::BanEntry> &bans);
    void guildInvitesFetched(Core::Snowflake guildId, const QList<Discord::InviteData> &invites);
    void guildWebhooksFetched(Core::Snowflake guildId, const QList<Discord::WebhookData> &webhooks);
    void guildIntegrationsFetched(Core::Snowflake guildId, const QList<Discord::IntegrationData> &integrations);
    void auditLogFetched(Core::Snowflake guildId, const Discord::AuditLogData &log);

    void reconnecting(int attempt, int maxAttempts);
    void errorOccurred(const QString &errorStr);
    void authenticationFailed();

private slots:
    void onConnected();
    void onDisconnected(CloseCode code, const QString &reason);

    void onGatewayReady(const Ready &data);
    void onGatewayReadySupplemental(const ReadySupplemental &data);
    void onGatewayMessageCreate(const Message &msg);
    void onGatewayMessageUpdate(const Message &msg);
    void onGatewayMessageDelete(const MessageDelete &event);
    void onGatewayUserUpdate(const User &user);
    void onGatewayChannelCreate(const ChannelCreate &event);
    void onGatewayChannelUpdate(const ChannelUpdate &event);
    void onGatewayChannelDelete(const ChannelDelete &event);
    void onGatewayThreadCreate(const ChannelCreate &event);
    void onGatewayThreadUpdate(const ChannelUpdate &event);
    void onGatewayThreadDelete(const ThreadDelete &event);
    void onGatewayThreadListSync(const ThreadListSync &event);
    void onGatewayGuildCreate(const GatewayGuild &guild);
    void onGatewayGuildDelete(const GuildDelete &event);
    void onGatewayGuildRoleCreate(const GuildRoleCreate &event);
    void onGatewayGuildRoleUpdate(const GuildRoleUpdate &event);
    void onGatewayGuildRoleDelete(const GuildRoleDelete &event);

private:
    void indexGuildMappings(const GatewayGuild &guild);
    void removeGuildMappings(Snowflake guildId);

    struct UploadState
    {
        Core::Snowflake channelId;
        QJsonObject payload;
        QString nonce;
        QList<Core::PendingAttachment> attachments;
        QStringList uploadFilenames;
        QList<bool> uploaded;
        int remaining = 0;
        bool failed = false;
        std::shared_ptr<std::atomic<bool>> cancelFlag;

        std::function<void(const QJsonArray &attachmentsJson)> onUploaded;
        std::function<void(const QString &error)> onFailed;
    };

    void setState(Core::ConnectionState state);
    void uploadAttachmentsAndSend(const std::shared_ptr<UploadState> &state);
    void finishUpload(const std::shared_ptr<UploadState> &state);
    void failUpload(const std::shared_ptr<UploadState> &state, const QString &error);
    void postForumThread(Snowflake forumId, const QString &name,
                         const QList<Snowflake> &appliedTags,
                         const QJsonObject &message,
                         ForumThreadCallback callback);
    void cleanupUploadedSlots(const std::shared_ptr<UploadState> &state);
    void settleUpload(const std::shared_ptr<UploadState> &state);

    // Persisted temp-ban unbans (survive app restart).
    void restorePendingUnbans();
    void schedulePendingUnban(Snowflake guildId, Snowflake userId, qint64 unbanAtMs);
    void removePendingUnban(Snowflake guildId, Snowflake userId);
    QHash<QPair<Snowflake, Snowflake>, QPointer<QTimer>> m_pendingUnbanTimers;

private:
    Core::ConnectionState state = Core::ConnectionState::Disconnected;

    QString baseUrl;
    QString token;

    ClientIdentity identity;
    HttpClient *httpClient;
    Gateway *gateway;

    QHash<Core::Snowflake, Core::Snowflake> channelToGuild; // todo prob move this somewhere or just a cache
    QHash<Core::Snowflake, PremiumTier> guildPremiumTiers;
    QSet<Core::Snowflake> subscribedGuilds;
    QHash<QString, std::shared_ptr<UploadState>> activeUploads; // by nonce

    Proto::PreloadedUserSettings settings;
    User me;
    QString m_lastPresenceStatus; // re-applied after reconnect/identify
};

} // namespace Discord
} // namespace Acheron
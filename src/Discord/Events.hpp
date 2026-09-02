#pragma once

#include <QHash>
#include <QString>

#include "Core/JsonUtils.hpp"
#include "Core/Snowflake.hpp"
#include "Entities.hpp"

namespace Acheron {
namespace Discord {

struct Ready : Core::JsonUtils::JsonObject
{
    Field<User> user;
    Field<QList<GatewayGuild>> guilds;
    Field<QString> userSettingsProto;
    Field<QList<QList<Member>>, true> mergedMembers;
    Field<QList<User>, true> users;
    Field<QList<Channel>, true> privateChannels;
    Field<QList<ReadStateEntry>, true> readState;
    Field<QList<UserGuildSettings>, true> userGuildSettings;
    Field<UserSettings, true> userSettings;
    Field<QList<Relationship>> relationships;
    Field<QString> sessionId;
    Field<QString> resumeGatewayUrl;
    Field<QHash<Core::Snowflake, QString>> notes;

    static Ready fromJson(const QJsonObject &obj)
    {
        Ready ready;
        get(obj, "user", ready.user);
        get(obj, "guilds", ready.guilds);
        get(obj, "user_settings_proto", ready.userSettingsProto);
        get(obj, "user_settings", ready.userSettings);
        get(obj, "merged_members", ready.mergedMembers);
        get(obj, "users", ready.users);
        get(obj, "private_channels", ready.privateChannels);
        get(obj, "relationships", ready.relationships);
        get(obj, "session_id", ready.sessionId);
        get(obj, "resume_gateway_url", ready.resumeGatewayUrl);
        get(obj, "notes", ready.notes);

        if (obj.contains("read_state")) {
            QJsonValue rsVal = obj["read_state"];
            if (rsVal.isObject()) {
                QJsonObject rsObj = rsVal.toObject();
                if (rsObj.contains("entries")) {
                    QJsonArray arr = rsObj["entries"].toArray();
                    QList<ReadStateEntry> entries;
                    entries.reserve(arr.size());
                    for (const QJsonValue &val : arr)
                        entries.append(ReadStateEntry::fromJson(val.toObject()));
                    ready.readState = entries;
                }
            } else if (rsVal.isArray()) {
                QJsonArray arr = rsVal.toArray();
                QList<ReadStateEntry> entries;
                entries.reserve(arr.size());
                for (const QJsonValue &val : arr)
                    entries.append(ReadStateEntry::fromJson(val.toObject()));
                ready.readState = entries;
            }
        }

        if (obj.contains("user_guild_settings")) {
            QJsonValue ugsVal = obj["user_guild_settings"];
            if (ugsVal.isObject()) {
                QJsonObject ugsObj = ugsVal.toObject();
                if (ugsObj.contains("entries")) {
                    QJsonArray arr = ugsObj["entries"].toArray();
                    QList<UserGuildSettings> entries;
                    entries.reserve(arr.size());
                    for (const QJsonValue &val : arr)
                        entries.append(UserGuildSettings::fromJson(val.toObject()));
                    ready.userGuildSettings = entries;
                }
            } else if (ugsVal.isArray()) {
                QJsonArray arr = ugsVal.toArray();
                QList<UserGuildSettings> entries;
                entries.reserve(arr.size());
                for (const QJsonValue &val : arr)
                    entries.append(UserGuildSettings::fromJson(val.toObject()));
                ready.userGuildSettings = entries;
            }
        }

        return ready;
    }
};

struct SupplementalGuild : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<QList<VoiceState>, true> voiceStates;

    static SupplementalGuild fromJson(const QJsonObject &obj)
    {
        SupplementalGuild supplementalGuild;
        get(obj, "id", supplementalGuild.id);
        get(obj, "voice_states", supplementalGuild.voiceStates);
        return supplementalGuild;
    }
};

struct ReadySupplemental : Core::JsonUtils::JsonObject
{
    Field<QList<SupplementalGuild>> guilds;
    Field<QList<QList<Member>>> mergedMembers;

    static ReadySupplemental fromJson(const QJsonObject &obj)
    {
        ReadySupplemental readySupplemental;
        get(obj, "guilds", readySupplemental.guilds);
        get(obj, "merged_members", readySupplemental.mergedMembers);
        return readySupplemental;
    }
};

struct TypingStart : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake, true> guildId;
    Field<Core::Snowflake> userId;
    Field<QDateTime> timestamp;
    Field<Member, true> member;

    static TypingStart fromJson(const QJsonObject &obj)
    {
        TypingStart typingStart;
        get(obj, "channel_id", typingStart.channelId);
        get(obj, "guild_id", typingStart.guildId);
        get(obj, "user_id", typingStart.userId);
        get(obj, "member", typingStart.member);
        // Discord sends this as Unix time in milliseconds (all Discord
        // timestamps use ms); fromSecsSinceEpoch would shift it by 1000x.
        typingStart.timestamp = QDateTime::fromMSecsSinceEpoch(obj["timestamp"].toVariant().toLongLong());
        return typingStart;
    }
};

struct ChannelCreate : Core::JsonUtils::JsonObject
{
    Field<Channel> channel;

    static ChannelCreate fromJson(const QJsonObject &obj)
    {
        ChannelCreate event;
        event.channel = Channel::fromJson(obj);
        return event;
    }
};

struct ChannelUpdate : Core::JsonUtils::JsonObject
{
    Field<Channel> channel;

    static ChannelUpdate fromJson(const QJsonObject &obj)
    {
        ChannelUpdate update;
        update.channel = Channel::fromJson(obj);
        return update;
    }
};

struct ChannelDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake, true> guildId;

    static ChannelDelete fromJson(const QJsonObject &obj)
    {
        ChannelDelete event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        return event;
    }
};

struct ForumUnread : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> threadId;
    Field<int, true> count;

    static ForumUnread fromJson(const QJsonObject &obj)
    {
        ForumUnread unread;
        get(obj, "thread_id", unread.threadId);
        get(obj, "count", unread.count);
        return unread;
    }
};

struct ForumUnreads : Core::JsonUtils::JsonObject
{
    Field<QList<ForumUnread>, true> threads;
    Field<bool, true> permissionDenied;

    static ForumUnreads fromJson(const QJsonObject &obj)
    {
        ForumUnreads event;
        get(obj, "threads", event.threads);
        get(obj, "permission_denied", event.permissionDenied);
        return event;
    }
};

struct ThreadListSync : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<QList<Core::Snowflake>, true> channelIds;
    Field<QList<Channel>, true> threads;
    Field<QList<ThreadMember>, true> members;

    static ThreadListSync fromJson(const QJsonObject &obj)
    {
        ThreadListSync event;
        get(obj, "guild_id", event.guildId);
        get(obj, "channel_ids", event.channelIds);
        get(obj, "threads", event.threads);
        get(obj, "members", event.members);
        return event;
    }
};

struct ThreadMemberUpdate : Core::JsonUtils::JsonObject
{
    ThreadMember member;
    Field<Core::Snowflake, true> userId;
    Field<Core::Snowflake, true> guildId;

    static ThreadMemberUpdate fromJson(const QJsonObject &obj)
    {
        ThreadMemberUpdate event;
        // Discord nests the member object: {id, guild_id, member: {id,
        // user_id, join_timestamp, flags}}. There is no top-level "user_id" —
        // parsing the outer object directly left event.userId Undefined and
        // the caller's "is this the current account" guard dead (any user
        // joining a cached thread was treated as the account itself).
        event.member = ThreadMember::fromJson(obj.value(QLatin1String("member")).toObject());
        if (event.member.userId.hasValue())
            event.userId = event.member.userId.get();
        get(obj, "guild_id", event.guildId);
        return event;
    }
};

struct ThreadMembersUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id; // thread id
    Field<Core::Snowflake, true> guildId;
    Field<int, true> memberCount;
    Field<QList<ThreadMember>, true> addedMembers;
    Field<QList<Core::Snowflake>, true> removedMemberIds;

    static ThreadMembersUpdate fromJson(const QJsonObject &obj)
    {
        ThreadMembersUpdate event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "member_count", event.memberCount);
        get(obj, "added_members", event.addedMembers);
        get(obj, "removed_member_ids", event.removedMemberIds);
        return event;
    }
};

struct GuildMembersChunk : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<QList<Member>> members;
    Field<int> chunkIndex;
    Field<int> chunkCount;
    Field<QList<Core::Snowflake>, true> notFound;

    static GuildMembersChunk fromJson(const QJsonObject &obj)
    {
        GuildMembersChunk chunk;
        get(obj, "guild_id", chunk.guildId);
        get(obj, "members", chunk.members);
        get(obj, "chunk_index", chunk.chunkIndex);
        get(obj, "chunk_count", chunk.chunkCount);
        get(obj, "not_found", chunk.notFound);
        return chunk;
    }
};

struct GuildMemberUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<Member> member;

    static GuildMemberUpdate fromJson(const QJsonObject &obj)
    {
        GuildMemberUpdate event;
        get(obj, "guild_id", event.guildId);
        event.member = Member::fromJson(obj);
        return event;
    }
};

struct MessageDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake, true> guildId;

    static MessageDelete fromJson(const QJsonObject &obj)
    {
        MessageDelete event;
        get(obj, "id", event.id);
        get(obj, "channel_id", event.channelId);
        get(obj, "guild_id", event.guildId);
        return event;
    }
};

struct MessageDeleteBulk : Core::JsonUtils::JsonObject
{
    Field<QList<Core::Snowflake>> ids;
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake, true> guildId;

    static MessageDeleteBulk fromJson(const QJsonObject &obj)
    {
        MessageDeleteBulk event;
        get(obj, "ids", event.ids);
        get(obj, "channel_id", event.channelId);
        get(obj, "guild_id", event.guildId);
        return event;
    }
};

// GUILD_MEMBER_ADD has the same payload shape as GUILD_MEMBER_UPDATE
// (member object plus guild_id), so it reuses the GuildMemberUpdate struct.

struct GuildMemberRemove : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<User> user;

    static GuildMemberRemove fromJson(const QJsonObject &obj)
    {
        GuildMemberRemove event;
        get(obj, "guild_id", event.guildId);
        get(obj, "user", event.user);
        return event;
    }
};

struct GuildRoleCreate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<Role> role;

    static GuildRoleCreate fromJson(const QJsonObject &obj)
    {
        GuildRoleCreate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "role", event.role);
        return event;
    }
};

struct GuildRoleUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<Role> role;

    static GuildRoleUpdate fromJson(const QJsonObject &obj)
    {
        GuildRoleUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "role", event.role);
        return event;
    }
};

struct GuildRoleDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> roleId;

    static GuildRoleDelete fromJson(const QJsonObject &obj)
    {
        GuildRoleDelete event;
        get(obj, "guild_id", event.guildId);
        get(obj, "role_id", event.roleId);
        return event;
    }
};

struct GuildDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<bool, true> unavailable;

    [[nodiscard]] bool userRemoved() const { return !(unavailable.hasValue() && unavailable.get()); }

    static GuildDelete fromJson(const QJsonObject &obj)
    {
        GuildDelete event;
        get(obj, "id", event.id);
        get(obj, "unavailable", event.unavailable);
        return event;
    }
};

struct MessageAck : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<int, true> mentionCount;
    Field<int, true> flags;
    Field<int, true> version;

    static MessageAck fromJson(const QJsonObject &obj)
    {
        MessageAck ack;
        get(obj, "channel_id", ack.channelId);
        get(obj, "message_id", ack.messageId);
        get(obj, "mention_count", ack.mentionCount);
        get(obj, "flags", ack.flags);
        get(obj, "version", ack.version);
        return ack;
    }
};

struct MessageReactionAdd : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> userId;
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<Core::Snowflake, true> messageAuthorId;
    Field<Core::Snowflake, true> guildId;
    Field<Emoji> emoji;
    Field<int, true> type; // 0 = normal, 1 = burst
    Field<QList<QString>, true> burstColors;

    static MessageReactionAdd fromJson(const QJsonObject &obj)
    {
        MessageReactionAdd event;
        get(obj, "user_id", event.userId);
        get(obj, "channel_id", event.channelId);
        get(obj, "message_id", event.messageId);
        get(obj, "message_author_id", event.messageAuthorId);
        get(obj, "guild_id", event.guildId);
        get(obj, "emoji", event.emoji);
        get(obj, "type", event.type);
        get(obj, "burst_colors", event.burstColors);
        return event;
    }
};

struct DebouncedReaction : Core::JsonUtils::JsonObject
{
    Field<QList<Core::Snowflake>> users;
    Field<Emoji> emoji;

    static DebouncedReaction fromJson(const QJsonObject &obj)
    {
        DebouncedReaction reaction;
        get(obj, "users", reaction.users);
        get(obj, "emoji", reaction.emoji);
        return reaction;
    }
};

struct MessageReactionAddMany : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<Core::Snowflake, true> guildId;
    Field<QList<DebouncedReaction>> reactions;

    static MessageReactionAddMany fromJson(const QJsonObject &obj)
    {
        MessageReactionAddMany event;
        get(obj, "channel_id", event.channelId);
        get(obj, "message_id", event.messageId);
        get(obj, "guild_id", event.guildId);
        get(obj, "reactions", event.reactions);
        return event;
    }
};

struct MessageReactionRemove : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> userId;
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<Core::Snowflake, true> guildId;
    Field<Emoji> emoji;
    Field<int, true> type;

    static MessageReactionRemove fromJson(const QJsonObject &obj)
    {
        MessageReactionRemove event;
        get(obj, "user_id", event.userId);
        get(obj, "channel_id", event.channelId);
        get(obj, "message_id", event.messageId);
        get(obj, "guild_id", event.guildId);
        get(obj, "emoji", event.emoji);
        get(obj, "type", event.type);
        return event;
    }
};

struct MessageReactionRemoveAll : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<Core::Snowflake, true> guildId;

    static MessageReactionRemoveAll fromJson(const QJsonObject &obj)
    {
        MessageReactionRemoveAll event;
        get(obj, "channel_id", event.channelId);
        get(obj, "message_id", event.messageId);
        get(obj, "guild_id", event.guildId);
        return event;
    }
};

struct MessageReactionRemoveEmoji : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake> messageId;
    Field<Core::Snowflake, true> guildId;
    Field<Emoji> emoji;

    static MessageReactionRemoveEmoji fromJson(const QJsonObject &obj)
    {
        MessageReactionRemoveEmoji event;
        get(obj, "channel_id", event.channelId);
        get(obj, "message_id", event.messageId);
        get(obj, "guild_id", event.guildId);
        get(obj, "emoji", event.emoji);
        return event;
    }
};

struct GuildMemberListUpdate : Core::JsonUtils::JsonObject
{
    struct Group : Core::JsonUtils::JsonObject
    {
        Field<QString> id; // role snowflake string, "online", or "offline"
        Field<int> count;

        static Group fromJson(const QJsonObject &obj)
        {
            Group group;
            get(obj, "id", group.id);
            get(obj, "count", group.count);
            return group;
        }
    };

    struct SyncItem : Core::JsonUtils::JsonObject
    {
        Field<Group, true> group;
        Field<Member, true> member;

        static SyncItem fromJson(const QJsonObject &obj)
        {
            SyncItem item;
            if (obj.contains("group"))
                item.group = Group::fromJson(obj["group"].toObject());
            if (obj.contains("member"))
                item.member = Member::fromJson(obj["member"].toObject());
            return item;
        }
    };

    struct ListOp : Core::JsonUtils::JsonObject
    {
        Field<QString> op;
        Field<QPair<int, int>, true> range;
        Field<QList<SyncItem>, true> items;
        Field<int, true> index;
        Field<SyncItem, true> item;

        static ListOp fromJson(const QJsonObject &obj)
        {
            ListOp listOp;
            get(obj, "op", listOp.op);
            get(obj, "index", listOp.index);

            if (obj.contains("range")) {
                QJsonArray rangeArr = obj["range"].toArray();
                if (rangeArr.size() == 2)
                    listOp.range = QPair<int, int>(rangeArr[0].toInt(), rangeArr[1].toInt());
            }

            if (obj.contains("items")) {
                QJsonArray itemsArr = obj["items"].toArray();
                QList<SyncItem> syncItems;
                syncItems.reserve(itemsArr.size());
                for (const QJsonValue &val : itemsArr)
                    syncItems.append(SyncItem::fromJson(val.toObject()));
                listOp.items = syncItems;
            }

            if (obj.contains("item"))
                listOp.item = SyncItem::fromJson(obj["item"].toObject());

            return listOp;
        }
    };

    Field<QString> id; // member list id
    Field<Core::Snowflake> guildId;
    Field<QList<Group>> groups;
    Field<QList<ListOp>> ops;
    Field<int> memberCount;
    Field<int> onlineCount;

    static GuildMemberListUpdate fromJson(const QJsonObject &obj)
    {
        GuildMemberListUpdate event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "member_count", event.memberCount);
        get(obj, "online_count", event.onlineCount);

        if (obj.contains("groups")) {
            QJsonArray groupsArr = obj["groups"].toArray();
            QList<Group> groups;
            groups.reserve(groupsArr.size());
            for (const QJsonValue &val : groupsArr)
                groups.append(Group::fromJson(val.toObject()));
            event.groups = groups;
        }

        if (obj.contains("ops")) {
            QJsonArray opsArr = obj["ops"].toArray();
            QList<ListOp> ops;
            ops.reserve(opsArr.size());
            for (const QJsonValue &val : opsArr)
                ops.append(ListOp::fromJson(val.toObject()));
            event.ops = ops;
        }

        return event;
    }
};

struct VoiceServerUpdate : Core::JsonUtils::JsonObject
{
    Field<QString> token;
    Field<Core::Snowflake> guildId;
    Field<QString, false, true> endpoint;

    static VoiceServerUpdate fromJson(const QJsonObject &obj)
    {
        VoiceServerUpdate event;
        get(obj, "token", event.token);
        get(obj, "guild_id", event.guildId);
        get(obj, "endpoint", event.endpoint);
        return event;
    }
};

struct VoiceStateUpdateBatch : Core::JsonUtils::JsonObject
{
    Field<QList<VoiceState>, true> voiceStates;

    static VoiceStateUpdateBatch fromJson(const QJsonObject &obj)
    {
        VoiceStateUpdateBatch event;
        get(obj, "voice_states", event.voiceStates);
        return event;
    }
};

struct RelationshipPartial : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<RelationshipType, true> type;
    Field<QString, true, true> nickname;
    Field<QDateTime, true> since;
    Field<bool, true> strangerRequest;
    Field<bool, true> userIgnored;

    static RelationshipPartial fromJson(const QJsonObject &obj)
    {
        RelationshipPartial r;
        get(obj, "id", r.id);
        get(obj, "type", r.type);
        get(obj, "nickname", r.nickname);
        get(obj, "since", r.since);
        get(obj, "stranger_request", r.strangerRequest);
        get(obj, "user_ignored", r.userIgnored);
        return r;
    }
};

struct UserNoteUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<QString> note;

    static UserNoteUpdate fromJson(const QJsonObject &obj)
    {
        UserNoteUpdate n;
        get(obj, "id", n.id);
        get(obj, "note", n.note);
        return n;
    }
};

// === GUILD EVENTS ===

struct GuildBan : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<User> user;

    static GuildBan fromJson(const QJsonObject &obj)
    {
        GuildBan event;
        get(obj, "guild_id", event.guildId);
        get(obj, "user", event.user);
        return event;
    }
};

struct GuildEmojisUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<QList<Emoji>> emojis;

    static GuildEmojisUpdate fromJson(const QJsonObject &obj)
    {
        GuildEmojisUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "emojis", event.emojis);
        return event;
    }
};

struct GuildStickersUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<QList<Sticker>> stickers;

    static GuildStickersUpdate fromJson(const QJsonObject &obj)
    {
        GuildStickersUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "stickers", event.stickers);
        return event;
    }
};

// === THREAD EVENTS ===

struct ThreadCreate : Core::JsonUtils::JsonObject
{
    Field<Channel> channel;

    static ThreadCreate fromJson(const QJsonObject &obj)
    {
        ThreadCreate event;
        event.channel = Channel::fromJson(obj);
        return event;
    }
};

struct ThreadUpdate : Core::JsonUtils::JsonObject
{
    Field<Channel> channel;

    static ThreadUpdate fromJson(const QJsonObject &obj)
    {
        ThreadUpdate event;
        event.channel = Channel::fromJson(obj);
        return event;
    }
};

struct ThreadDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> parentId;
    Field<ChannelType> type;

    static ThreadDelete fromJson(const QJsonObject &obj)
    {
        ThreadDelete event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "parent_id", event.parentId);
        get(obj, "type", event.type);
        return event;
    }
};

// === PRESENCE ===

struct Activity : Core::JsonUtils::JsonObject
{
    Field<QString> name;
    Field<int> type;
    Field<QString, true, true> url;
    Field<QDateTime, true> createdAt;
    Field<QString, true, true> details;
    Field<QString, true, true> state;
    Field<Core::Snowflake, true, true> applicationId;
    Field<QString, true, true> emoji;
    Field<int, true> flags;

    static Activity fromJson(const QJsonObject &obj)
    {
        Activity act;
        get(obj, "name", act.name);
        get(obj, "type", act.type);
        get(obj, "url", act.url);
        get(obj, "created_at", act.createdAt);
        get(obj, "details", act.details);
        get(obj, "state", act.state);
        get(obj, "application_id", act.applicationId);
        get(obj, "emoji", act.emoji);
        get(obj, "flags", act.flags);
        return act;
    }
};

// Per-device presence from client_status: each device's status, or empty when
// the user isn't active on that device.
struct ClientStatus : Core::JsonUtils::JsonObject
{
    Field<QString, true, true> desktop;
    Field<QString, true, true> mobile;
    Field<QString, true, true> web;

    static ClientStatus fromJson(const QJsonObject &obj)
    {
        ClientStatus cs;
        get(obj, "desktop", cs.desktop);
        get(obj, "mobile", cs.mobile);
        get(obj, "web", cs.web);
        return cs;
    }
};

struct PresenceUpdate : Core::JsonUtils::JsonObject
{
    Field<User> user;
    Field<Core::Snowflake, true> guildId;
    Field<QString> status;
    Field<QList<Activity>, true> activities;
    Field<ClientStatus, true> clientStatus;

    static PresenceUpdate fromJson(const QJsonObject &obj)
    {
        PresenceUpdate presence;
        get(obj, "user", presence.user);
        get(obj, "guild_id", presence.guildId);
        get(obj, "status", presence.status);
        get(obj, "activities", presence.activities);
        get(obj, "client_status", presence.clientStatus);
        return presence;
    }
};

// === WEBHOOKS & INVITES ===

struct WebhooksUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> channelId;

    static WebhooksUpdate fromJson(const QJsonObject &obj)
    {
        WebhooksUpdate event;
        get(obj, "guild_id", event.guildId);
        get(obj, "channel_id", event.channelId);
        return event;
    }
};

struct InviteCreate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<QString> code;
    Field<QDateTime> createdAt;
    Field<Core::Snowflake, true> guildId;
    Field<User, true> inviter;
    Field<int, true> maxAge;
    Field<int, true> maxUses;
    Field<int, true> uses;
    Field<bool, true> temporary;
    Field<int, true> channelType;

    static InviteCreate fromJson(const QJsonObject &obj)
    {
        InviteCreate event;
        get(obj, "channel_id", event.channelId);
        get(obj, "code", event.code);
        get(obj, "created_at", event.createdAt);
        get(obj, "guild_id", event.guildId);
        get(obj, "inviter", event.inviter);
        get(obj, "max_age", event.maxAge);
        get(obj, "max_uses", event.maxUses);
        get(obj, "uses", event.uses);
        get(obj, "temporary", event.temporary);
        get(obj, "channel_type", event.channelType);
        return event;
    }
};

struct InviteDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake, true> guildId;
    Field<QString> code;

    static InviteDelete fromJson(const QJsonObject &obj)
    {
        InviteDelete event;
        get(obj, "channel_id", event.channelId);
        get(obj, "guild_id", event.guildId);
        get(obj, "code", event.code);
        return event;
    }
};

// === STAGE INSTANCES ===

struct StageInstance : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake> channelId;
    Field<QString> topic;
    Field<int> privacyLevel;
    Field<bool, true> discoverableDisabled;

    static StageInstance fromJson(const QJsonObject &obj)
    {
        StageInstance instance;
        get(obj, "id", instance.id);
        get(obj, "guild_id", instance.guildId);
        get(obj, "channel_id", instance.channelId);
        get(obj, "topic", instance.topic);
        get(obj, "privacy_level", instance.privacyLevel);
        get(obj, "discoverable_disabled", instance.discoverableDisabled);
        return instance;
    }
};

// === SCHEDULED EVENTS ===

struct GuildScheduledEvent : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<Core::Snowflake, true> channelId;
    Field<QString> name;
    Field<QString, true> description;
    Field<QDateTime> scheduledStartTime;
    Field<QDateTime, true> scheduledEndTime;
    Field<int> privacyLevel;
    Field<int> status;
    Field<int> entityType;
    Field<Core::Snowflake, true> entityId;
    Field<User, true> creator;

    static GuildScheduledEvent fromJson(const QJsonObject &obj)
    {
        GuildScheduledEvent event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "channel_id", event.channelId);
        get(obj, "name", event.name);
        get(obj, "description", event.description);
        get(obj, "scheduled_start_time", event.scheduledStartTime);
        get(obj, "scheduled_end_time", event.scheduledEndTime);
        get(obj, "privacy_level", event.privacyLevel);
        get(obj, "status", event.status);
        get(obj, "entity_type", event.entityType);
        get(obj, "entity_id", event.entityId);
        get(obj, "creator", event.creator);
        return event;
    }
};

// === INTEGRATIONS ===

struct IntegrationCreate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<QString> type;
    Field<QString, true> name;

    static IntegrationCreate fromJson(const QJsonObject &obj)
    {
        IntegrationCreate event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "type", event.type);
        get(obj, "name", event.name);
        return event;
    }
};

struct IntegrationUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<QString> type;
    Field<QString, true> name;

    static IntegrationUpdate fromJson(const QJsonObject &obj)
    {
        IntegrationUpdate event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "type", event.type);
        get(obj, "name", event.name);
        return event;
    }
};

struct IntegrationDelete : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> id;
    Field<Core::Snowflake> guildId;
    Field<QString, true> applicationId;

    static IntegrationDelete fromJson(const QJsonObject &obj)
    {
        IntegrationDelete event;
        get(obj, "id", event.id);
        get(obj, "guild_id", event.guildId);
        get(obj, "application_id", event.applicationId);
        return event;
    }
};

// === CHANNEL PINS ===

struct ChannelPinsUpdate : Core::JsonUtils::JsonObject
{
    Field<Core::Snowflake> channelId;
    Field<Core::Snowflake, true> guildId;
    Field<QDateTime, true> lastPinTimestamp;

    static ChannelPinsUpdate fromJson(const QJsonObject &obj)
    {
        ChannelPinsUpdate event;
        get(obj, "channel_id", event.channelId);
        get(obj, "guild_id", event.guildId);
        get(obj, "last_pin_timestamp", event.lastPinTimestamp);
        return event;
    }
};

} // namespace Discord
} // namespace Acheron

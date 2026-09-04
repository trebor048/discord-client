#pragma once

#include <QObject>
#include <QList>
#include <QColor>
#include <QString>
#include <QTimer>

#include "Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

namespace Acheron {

namespace Storage {
class ChannelRepository;
class RoleRepository;
} // namespace Storage

namespace Core {

struct MemberListItem
{
    enum class Type {
        Group,
        Member,
        Placeholder
    };
    Type type = Type::Placeholder;

    QString groupId; // role snowflake string, "online", or "offline"
    QString groupName;
    int groupCount = 0;
    QColor groupColor;

    Discord::Member member;
    Snowflake userId;
    QString displayName;
    QColor roleColor;
    // Top-position role that has a custom icon (rendered as a small badge).
    Snowflake roleIconRoleId;
    QString roleIconHash;
};

struct ListData
{
    QHash<int, MemberListItem> items;
    QList<Discord::GuildMemberListUpdate::Group> groups;
    qint64 lastUsed = 0; // ms since epoch, for LRU eviction
};

struct GuildListState
{
    QHash<QString, ListData> lists; // listId -> items + groups
    // Keyed by the role snowflake itself; every member-role lookup previously
    // formatted a QString::number() per (member, role) pair.
    QHash<Snowflake, Discord::Role> roleCache;
    int memberCount = 0;
    int onlineCount = 0;
};

class MemberListManager : public QObject
{
    Q_OBJECT
public:
    explicit MemberListManager(Storage::ChannelRepository &channelRepo,
                               Storage::RoleRepository &roleRepo,
                               QObject *parent = nullptr);

    void setActiveChannel(Snowflake guildId, Snowflake channelId);
    void clear();
    void clearGuild(Snowflake guildId);

    void handleMemberListUpdate(const Discord::GuildMemberListUpdate &update);

    void handleMemberAdded(Snowflake guildId, const Discord::Member &member);
    void handleMemberRemoved(Snowflake guildId, Snowflake userId);

    void handleRoleCreated(Snowflake guildId, const Discord::Role &role);
    void handleRoleUpdated(Snowflake guildId, const Discord::Role &role);
    void handleRoleDeleted(Snowflake guildId, Snowflake roleId);

    void updateSubscriptionRange(int firstVisible, int lastVisible);

    // Re-issues the member-list subscription for the currently open channel.
    // A reconnect that falls back to a fresh IDENTIFY makes Discord drop every
    // lazy member-list subscription, and the active channel does not change,
    // so nothing else would re-request it. No-op when no channel is active.
    void refreshActiveSubscription();

    // virtual row count (cached; invalidated only on list reset/update)
    [[nodiscard]] int totalItemCount() const;

    // virtual index
    [[nodiscard]] const MemberListItem *itemAt(int index) const;

    [[nodiscard]] bool isLoaded(int index) const;
    [[nodiscard]] Snowflake currentGuildId() const { return activeGuildId; }
    [[nodiscard]] Snowflake currentChannelId() const { return activeChannelId; }
    [[nodiscard]] int totalMemberCount() const;
    [[nodiscard]] int onlineCount() const;
    [[nodiscard]] const QList<QPair<int, int>> &currentRanges() const { return ranges; }

    static QString computeListId(const QList<Discord::PermissionOverwrite> &overwrites, Discord::Permissions everyonePermissions);

signals:
    void listAboutToReset();
    void listReset();
    // Emitted for batches that only update in-place rows (presence/display
    // changes) with no structural reordering. Consumers should repaint just
    // these rows instead of resetting the whole model.
    void listRowsChanged(const QList<int> &rows);

    void subscriptionRequested(Snowflake guildId, Snowflake channelId, const QList<QPair<int, int>> &ranges);

private:
    GuildListState *activeGuildState();
    const GuildListState *activeGuildState() const;
    ListData *activeListData();
    const ListData *activeListData() const;

    MemberListItem syncItemToListItem(const Discord::GuildMemberListUpdate::SyncItem &syncItem,
                                      const GuildListState &guildState, const ListData &listData);
    void resolveGroupInfo(MemberListItem &item, const GuildListState &guildState,
                          const ListData &listData);
    void resolveMemberInfo(MemberListItem &item, const GuildListState &guildState);
    void processSync(const Discord::GuildMemberListUpdate::ListOp &op, ListData &listData,
                     const GuildListState &guildState);
    void processBatchInserts(const QList<Discord::GuildMemberListUpdate::ListOp> &ops,
                             ListData &listData, const GuildListState &guildState);
    void processUpdate(const Discord::GuildMemberListUpdate::ListOp &op, ListData &listData,
                       const GuildListState &guildState);
    void processBatchDeletes(const QList<int> &indices, ListData &listData);
    void processInvalidate(const Discord::GuildMemberListUpdate::ListOp &op, ListData &listData);

    void evictUnsubscribedItems(const QList<QPair<int, int>> &oldRanges,
                                const QList<QPair<int, int>> &newRanges);
    void evictStaleLists(GuildListState &gs, Snowflake guildId);
    void applyAndSendRanges(const QList<QPair<int, int>> &newRanges);
    void flushPendingRanges();
    static bool indexInRanges(int index, const QList<QPair<int, int>> &ranges);

    void updateTotalItemCountCache();

    Storage::ChannelRepository &channelRepo;
    Storage::RoleRepository &roleRepo;

    QHash<Snowflake, GuildListState> guildStates;

    Snowflake activeGuildId;
    Snowflake activeChannelId;
    QString listId; // active list

    QList<QPair<int, int>> ranges;

    // avoid spamming
    QTimer responseTimer;
    bool awaitingResponse = false;
    bool hasPendingRanges = false;
    QList<QPair<int, int>> pendingRanges;

    int totalItemCountCache = 0;
};

} // namespace Core
} // namespace Acheron

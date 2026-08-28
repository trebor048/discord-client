#pragma once

#include <QObject>
#include <QCache>
#include <QPair>
#include <iterator>
#include <list>
#include <optional>

#include "Snowflake.hpp"
#include "Discord/Enums.hpp"
#include "Discord/Entities.hpp"

#include "Storage/GuildRepository.hpp"
#include "Storage/ChannelRepository.hpp"
#include "Storage/RoleRepository.hpp"
#include "Storage/MemberRepository.hpp"

namespace Acheron {
namespace Core {

class PermissionManager : public QObject
{
    Q_OBJECT
public:
    explicit PermissionManager(Snowflake accountId, QObject *parent = nullptr);

    Discord::Permissions getChannelPermissions(Snowflake userId, Snowflake channelId);
    bool hasChannelPermission(Snowflake userId, Snowflake channelId,
                              Discord::Permissions permission);

    void precomputeGuildPermissions(const Discord::Guild &guild, const Discord::Member &member,
                                    const QList<Discord::Role> &roles,
                                    const QList<Discord::Channel> &channels, Snowflake userId);

    void invalidateChannelCache(Snowflake channelId);
    void invalidateUserGuildCache(Snowflake userId, Snowflake guildId);

signals:
    void channelPermissionsChanged(Snowflake channelId);

private:
    using CacheKey = QPair<Snowflake /* userId */, Snowflake /* channelId */>;

    Discord::Permissions computeChannelPermissions(Snowflake userId, Snowflake channelId);
    void insertIntoCache(const CacheKey &key, Discord::Permissions permissions);

    Storage::RoleRepository roleRepo;
    Storage::GuildRepository guildRepo;
    Storage::ChannelRepository channelRepo;
    Storage::MemberRepository memberRepo;

    QHash<CacheKey, Discord::Permissions> permissionCache;
    // O(1) LRU: `std::list` front = least recently used, back = most recently
    // used. The index maps each key to its list iterator so cache hits can
    // promote without an O(n) `removeOne` scan (this is the sidebar's hot path).
    using LruList = std::list<CacheKey>;
    LruList permissionCacheLru;
    QHash<CacheKey, LruList::iterator> permissionCacheLruIndex;
};

} // namespace Core
} // namespace Acheron

#include "PermissionManager.hpp"
#include "PermissionComputer.hpp"
#include "Logging.hpp"

#include "Storage/RoleRepository.hpp"
#include "Storage/GuildRepository.hpp"
#include "Storage/ChannelRepository.hpp"
#include "Storage/MemberRepository.hpp"

namespace Acheron {
namespace Core {

namespace {
    constexpr int MaxPermissionCacheSize = 10000;
}

PermissionManager::PermissionManager(Snowflake accountId, QObject *parent)
    : QObject(parent),
      roleRepo(accountId),
      guildRepo(accountId),
      channelRepo(accountId),
      memberRepo(accountId)
{
}

void PermissionManager::insertIntoCache(const CacheKey &key, Discord::Permissions permissions)
{
    if (permissionCache.contains(key)) {
        permissionCache.insert(key, permissions);
        permissionCacheLru.removeOne(key);
        permissionCacheLru.append(key);
        return;
    }

    while (permissionCache.size() >= MaxPermissionCacheSize && !permissionCacheLru.isEmpty())
        permissionCache.remove(permissionCacheLru.takeFirst());

    permissionCache.insert(key, permissions);
    permissionCacheLru.append(key);
}

Discord::Permissions PermissionManager::getChannelPermissions(Snowflake userId, Snowflake channelId)
{
    auto cacheKey = qMakePair(userId, channelId);
    auto it = permissionCache.constFind(cacheKey);
    if (it != permissionCache.constEnd()) {
        // touch: most recently used goes to the back
        permissionCacheLru.removeOne(cacheKey);
        permissionCacheLru.append(cacheKey);
        return it.value();
    }

    auto permissions = computeChannelPermissions(userId, channelId);
    insertIntoCache(cacheKey, permissions);

    return permissions;
}

bool PermissionManager::hasChannelPermission(Snowflake userId, Snowflake channelId,
                                             Discord::Permissions permission)
{
    auto perms = getChannelPermissions(userId, channelId);
    return (perms & permission) == permission;
}

void PermissionManager::precomputeGuildPermissions(const Discord::Guild &guild,
                                                   const Discord::Member &member,
                                                   const QList<Discord::Role> &roles,
                                                   const QList<Discord::Channel> &channels,
                                                   Snowflake userId)
{
    QList<Snowflake> memberRoleIds;
    if (member.roles.hasValue())
        memberRoleIds = member.roles.get();

    for (const auto &channel : channels) {
        QList<Discord::PermissionOverwrite> overwrites;
        if (channel.permissionOverwrites.hasValue())
            overwrites = channel.permissionOverwrites.get();

        auto permissions = PermissionComputer::computeChannelPermissions(
                guild.ownerId.get(), userId, guild.id.get(), false, memberRoleIds, roles,
                overwrites);

        auto cacheKey = qMakePair(userId, channel.id.get());
        insertIntoCache(cacheKey, permissions);
    }
}

Discord::Permissions PermissionManager::computeChannelPermissions(Snowflake userId,
                                                                  Snowflake channelId)
{
    auto channelOpt = channelRepo.getChannel(channelId);
    if (!channelOpt) {
        qCWarning(LogCore) << "PermissionManager: Channel not found:" << channelId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &channel = *channelOpt;

    bool isDM = !channel.guildId.hasValue();

    if (isDM) {
        return PermissionComputer::computeChannelPermissions(Snowflake::Invalid, userId,
                                                             Snowflake::Invalid, true, {}, {}, {});
    }

    Snowflake guildId = channel.guildId.get();

    auto guildOpt = guildRepo.getGuild(guildId);
    if (!guildOpt) {
        qCWarning(LogCore) << "PermissionManager: Guild not found:" << guildId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &guild = *guildOpt;

    auto memberOpt = memberRepo.getMember(guildId, userId);
    if (!memberOpt) {
        qCWarning(LogCore) << "PermissionManager: Member not found:" << userId << "in" << guildId;
        return Discord::NO_PERMISSIONS;
    }

    const auto &member = *memberOpt;

    auto allRoles = roleRepo.getRolesForGuild(guildId);

    QList<Discord::PermissionOverwrite> overwrites;
    if (channel.permissionOverwrites.hasValue())
        overwrites = channel.permissionOverwrites.get();

    QList<Snowflake> memberRoleIds;
    if (member.roles.hasValue())
        memberRoleIds = member.roles.get();

    return PermissionComputer::computeChannelPermissions(
            guild.ownerId.get(), userId, guildId, false, memberRoleIds, allRoles, overwrites);
}

void PermissionManager::invalidateChannelCache(Snowflake channelId)
{
    auto it = permissionCache.begin();
    while (it != permissionCache.end()) {
        if (it.key().second == channelId) {
            permissionCacheLru.removeOne(it.key());
            it = permissionCache.erase(it);
        } else {
            ++it;
        }
    }

    qCDebug(LogCore) << "Invalidated permission cache for channel:" << channelId;
    emit channelPermissionsChanged(channelId);
}

void PermissionManager::invalidateUserGuildCache(Snowflake userId, Snowflake guildId)
{
    auto channels = channelRepo.getChannelsForGuild(guildId);

    QSet<Snowflake> channelIds;
    channelIds.reserve(channels.size());
    for (const auto &channel : channels)
        channelIds.insert(channel.id.get());

    QList<Snowflake> invalidated;
    auto it = permissionCache.begin();
    while (it != permissionCache.end()) {
        if (it.key().first == userId && channelIds.contains(it.key().second)) {
            invalidated.append(it.key().second);
            permissionCacheLru.removeOne(it.key());
            it = permissionCache.erase(it);
        } else {
            ++it;
        }
    }

    for (Snowflake channelId : invalidated)
        emit channelPermissionsChanged(channelId);

    qCDebug(LogCore) << "Invalidated" << invalidated.size() << "cached permissions for user" << userId << "in guild:" << guildId;
}

} // namespace Core
} // namespace Acheron

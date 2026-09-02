#include "PermissionComputer.hpp"

#include <algorithm>

namespace Acheron {
namespace Core {

namespace {

// computeBasePermissions() over a prebuilt role map. computeChannelPermissions
// builds the map once and reuses it here, avoiding a second O(N) copy per
// permission computation.
Discord::Permissions computeBasePermissionsWithMap(
        Snowflake guildOwnerId, Snowflake userId, Snowflake guildId,
        const QList<Snowflake> &memberRoleIds,
        const QHash<Snowflake, Discord::Role> &roleMap)
{
    Discord::Permissions permissions = Discord::NO_PERMISSIONS;
    if (auto everyoneRole = roleMap.value(guildId); everyoneRole.id.hasValue())
        permissions = everyoneRole.permissions.get();

    for (const auto &roleId : memberRoleIds) {
        if (auto role = roleMap.value(roleId); role.id.hasValue())
            permissions |= role.permissions.get();
    }

    if (permissions & Discord::Permission::ADMINISTRATOR)
        return Discord::ALL_PERMISSIONS;

    return permissions;
}

} // namespace

QHash<Snowflake, Discord::Role> PermissionComputer::buildRoleMap(const QList<Discord::Role> &roles)
{
    QHash<Snowflake, Discord::Role> roleMap;
    roleMap.reserve(roles.size());
    for (const auto &role : roles)
        roleMap.insert(role.id.get(), role);
    return roleMap;
}

Discord::Permissions PermissionComputer::computeBasePermissions(
        Snowflake guildOwnerId, Snowflake userId, Snowflake guildId,
        const QList<Snowflake> &memberRoleIds, const QList<Discord::Role> &allRoles)
{
    if (guildOwnerId == userId)
        return Discord::ALL_PERMISSIONS;

    auto roleMap = buildRoleMap(allRoles);

    return computeBasePermissionsWithMap(guildOwnerId, userId, guildId, memberRoleIds, roleMap);
}

Discord::Permissions PermissionComputer::computeOverwrites(
        Discord::Permissions basePermissions, Snowflake userId, Snowflake guildId,
        const QList<Snowflake> &memberRoleIds,
        const QList<Discord::PermissionOverwrite> &overwrites)
{
    if (basePermissions & Discord::Permission::ADMINISTRATOR)
        return Discord::ALL_PERMISSIONS;

    Discord::Permissions permissions = basePermissions;

    // @everyone
    for (const auto &ow : overwrites) {
        if (ow.type.get() == Discord::PermissionOverwrite::Type::Role && ow.id.get() == guildId) {
            permissions &= ~ow.deny.get();
            permissions |= ow.allow.get();
            break;
        }
    }

    // Apply role overwrites per Discord's documented algorithm: OR all role
    // `allow`s together and all role `deny`s together, then apply deny first
    // and allow second ONCE. Role overwrites do NOT follow the role hierarchy
    // (a lower role's allow still beats a higher role's deny), so the previous
    // sequential per-role application was wrong.
    Discord::Permissions roleAllow = Discord::NO_PERMISSIONS;
    Discord::Permissions roleDeny = Discord::NO_PERMISSIONS;
    for (const auto &roleId : memberRoleIds) {
        for (const auto &ow : overwrites) {
            if (ow.type.get() == Discord::PermissionOverwrite::Type::Role &&
                ow.id.get() == roleId) {
                roleDeny |= ow.deny.get();
                roleAllow |= ow.allow.get();
                break;
            }
        }
    }
    permissions &= ~roleDeny;
    permissions |= roleAllow;

    for (const auto &ow : overwrites) {
        if (ow.type.get() == Discord::PermissionOverwrite::Type::Member && ow.id.get() == userId) {
            permissions &= ~ow.deny.get();
            permissions |= ow.allow.get();
            break;
        }
    }

    return permissions;
}

Discord::Permissions PermissionComputer::computeChannelPermissions(
        Snowflake guildOwnerId, Snowflake userId, Snowflake guildId, bool isDM,
        const QList<Snowflake> &memberRoleIds, const QList<Discord::Role> &allRoles,
        const QList<Discord::PermissionOverwrite> &overwrites)
{
    if (isDM) {
        // clang-format off
        return Discord::Permission::VIEW_CHANNEL |
               Discord::Permission::SEND_MESSAGES |
               Discord::Permission::READ_MESSAGE_HISTORY |
               Discord::Permission::ATTACH_FILES |
               Discord::Permission::ADD_REACTIONS |
               Discord::Permission::EMBED_LINKS |
               Discord::Permission::USE_EXTERNAL_EMOJIS |
               Discord::Permission::MENTION_EVERYONE |
               Discord::Permission::USE_EXTERNAL_STICKERS |
               Discord::Permission::SEND_TTS_MESSAGES |
               Discord::Permission::USE_APPLICATION_COMMANDS;
        // clang-format on
    }

    if (guildOwnerId == userId)
        return Discord::ALL_PERMISSIONS;

    // Build the role map once and reuse it for the base-permission pass and the
    // position lookup below (the old code rebuilt the map inside
    // computeBasePermissions and scanned all roles per comparator call).
    auto roleMap = buildRoleMap(allRoles);

    auto basePerms = computeBasePermissionsWithMap(guildOwnerId, userId, guildId, memberRoleIds,
                                                   roleMap);

    if (basePerms & Discord::Permission::ADMINISTRATOR)
        return Discord::ALL_PERMISSIONS;

    // Discord applies role channel-overwrites in role-position order: the
    // highest-position role is applied last and therefore takes precedence.
    // Sort the member's roles so conflicting role overwrites resolve like
    // Discord does instead of in arbitrary array order.
    QHash<Snowflake, int> rolePositions;
    rolePositions.reserve(roleMap.size());
    for (auto it = roleMap.constBegin(); it != roleMap.constEnd(); ++it)
        rolePositions.insert(it.key(), it.value().position.get());

    QList<Snowflake> orderedRoles = memberRoleIds;
    std::sort(orderedRoles.begin(), orderedRoles.end(),
              [&rolePositions](const Snowflake &a, const Snowflake &b) {
                  const int posA = rolePositions.value(a, 0);
                  const int posB = rolePositions.value(b, 0);
                  if (posA != posB)
                      return posA < posB;
                  return a < b; // stable tie-break
              });

    return computeOverwrites(basePerms, userId, guildId, orderedRoles, overwrites);
}

} // namespace Core
} // namespace Acheron

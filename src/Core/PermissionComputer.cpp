#include "PermissionComputer.hpp"

#include <algorithm>

namespace Acheron {
namespace Core {

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

    // Apply role overwrites sequentially (deny first, then allow per role).
    // This approximates Discord's role-hierarchy order — higher-positioned
    // roles should be processed first, but without role positions here we
    // process in memberRoleIds order, which is at least sequential rather
    // than flat OR (which would let a lower role's allow cancel a higher
    // role's deny).
    for (const auto &roleId : memberRoleIds) {
        for (const auto &ow : overwrites) {
            if (ow.type.get() == Discord::PermissionOverwrite::Type::Role &&
                ow.id.get() == roleId) {
                permissions &= ~ow.deny.get();
                permissions |= ow.allow.get();
                break;
            }
        }
    }

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

    auto basePerms = computeBasePermissions(guildOwnerId, userId, guildId, memberRoleIds, allRoles);

    if (basePerms & Discord::Permission::ADMINISTRATOR)
        return Discord::ALL_PERMISSIONS;

    // Discord applies role channel-overwrites in role-position order: the
    // highest-position role is applied last and therefore takes precedence.
    // Sort the member's roles so conflicting role overwrites resolve like
    // Discord does instead of in arbitrary array order.
    QList<Snowflake> orderedRoles = memberRoleIds;
    std::sort(orderedRoles.begin(), orderedRoles.end(),
              [&allRoles](const Snowflake &a, const Snowflake &b) {
                  int posA = 0;
                  int posB = 0;
                  for (const auto &r : allRoles) {
                      if (r.id.get() == a)
                          posA = r.position.get();
                      if (r.id.get() == b)
                          posB = r.position.get();
                  }
                  if (posA != posB)
                      return posA < posB;
                  return a < b; // stable tie-break
              });

    return computeOverwrites(basePerms, userId, guildId, orderedRoles, overwrites);
}

} // namespace Core
} // namespace Acheron

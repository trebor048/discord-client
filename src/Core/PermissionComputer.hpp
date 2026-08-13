#pragma once

#include <QHash>

#include "Snowflake.hpp"
#include "Discord/Enums.hpp"
#include "Discord/Entities.hpp"

namespace Acheron {
namespace Core {

class PermissionComputer
{
public:
    static Discord::Permissions computeBasePermissions(Snowflake guildOwnerId, Snowflake userId,
                                                       Snowflake guildId,
                                                       const QList<Snowflake> &memberRoleIds,
                                                       const QList<Discord::Role> &allRoles);

    static Discord::Permissions computeOverwrites(
            Discord::Permissions basePermissions, Snowflake userId, Snowflake guildId,
            const QList<Snowflake> &memberRoleIds,
            const QList<Discord::PermissionOverwrite> &overwrites);

    static Discord::Permissions computeChannelPermissions(
            Snowflake guildOwnerId, Snowflake userId, Snowflake guildId, bool isDM,
            const QList<Snowflake> &memberRoleIds, const QList<Discord::Role> &allRoles,
            const QList<Discord::PermissionOverwrite> &overwrites);

private:
    // helper :3
    static QHash<Snowflake, Discord::Role> buildRoleMap(const QList<Discord::Role> &roles);
};

} // namespace Core
} // namespace Acheron

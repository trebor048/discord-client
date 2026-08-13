#pragma once

#include <optional>

#include "BaseRepository.hpp"

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

namespace Acheron {
namespace Storage {

class ChannelRepository : public BaseRepository
{
public:
    ChannelRepository(Core::Snowflake accountId);

    bool saveChannel(const Discord::Channel &channel, QSqlDatabase &db);
    bool deleteChannel(Core::Snowflake channelId, QSqlDatabase &db);
    bool savePermissionOverwrites(Core::Snowflake channelId,
                                  const QList<Discord::PermissionOverwrite> &overwrites,
                                  QSqlDatabase &db);
    bool saveChannelRecipients(Core::Snowflake channelId,
                               const QList<Core::Snowflake> &recipientIds,
                               QSqlDatabase &db);
    QList<Discord::PermissionOverwrite> getPermissionOverwrites(Core::Snowflake channelId);
    QList<Core::Snowflake> getChannelRecipientIds(Core::Snowflake channelId);

    QHash<Core::Snowflake, QList<Discord::PermissionOverwrite>> getPermissionOverwritesForGuild(
            Core::Snowflake guildId);

    std::optional<Discord::Channel> getChannel(Core::Snowflake channelId);
    QList<Discord::Channel> getChannelsForGuild(Core::Snowflake guildId);
    std::optional<Core::Snowflake> findDmChannelWithUser(Core::Snowflake userId);
};

} // namespace Storage
} // namespace Acheron

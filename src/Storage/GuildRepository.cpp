#include "GuildRepository.hpp"

#include "DatabaseManager.hpp"
#include "Core/Logging.hpp"

#include <QSqlQuery>
#include <QSqlError>

namespace Acheron {
namespace Storage {
GuildRepository::GuildRepository(Core::Snowflake accountId)
    : BaseRepository(DatabaseManager::getCacheConnectionName(accountId))
{
}

bool GuildRepository::saveGuild(const Discord::Guild &guild, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare(R"(
		INSERT INTO guilds
		(id, name, icon, owner_id)
		VALUES (:id, :name, :icon, :owner_id)
		ON CONFLICT(id) DO UPDATE SET name=excluded.name, icon=excluded.icon, owner_id=excluded.owner_id
    )");

    q.bindValue(":id", static_cast<qint64>(guild.id.get()));
    q.bindValue(":name", guild.name);
    q.bindValue(":icon", guild.icon);
    q.bindValue(":owner_id", static_cast<qint64>(guild.ownerId.get()));

    return execLogged(q, "GuildRepository: Save");
}

void GuildRepository::deleteGuild(Core::Snowflake guildId, QSqlDatabase &db)
{
    // channels/members/messages have no FK to guilds, so delete them
    // explicitly to avoid orphaning rows. Caller wraps this in a transaction.
    // Short-circuit on the first failed statement: continuing to delete on a
    // broken DB would only widen the partial-deletion window.
    QSqlQuery q(db);
    auto runDelete = [&](const char *sql, const char *what) {
        q.prepare(QLatin1String(sql));
        q.bindValue(":guild_id", static_cast<qint64>(guildId));
        return execLogged(q, what);
    };

    if (!runDelete(R"(
            DELETE FROM attachments WHERE message_id IN
            (SELECT id FROM messages WHERE channel_id IN
             (SELECT id FROM channels WHERE guild_id = :guild_id))
        )",
                   "GuildRepository: Delete guild attachments"))
        return;
    if (!runDelete(R"(
            DELETE FROM messages WHERE channel_id IN
            (SELECT id FROM channels WHERE guild_id = :guild_id)
        )",
                   "GuildRepository: Delete guild messages"))
        return;
    if (!runDelete("DELETE FROM members WHERE guild_id = :guild_id",
                   "GuildRepository: Delete guild members"))
        return;
    if (!runDelete(
                "DELETE FROM permission_overwrites WHERE channel_id IN (SELECT id FROM channels WHERE guild_id = :guild_id)",
                "GuildRepository: Delete guild overwrites"))
        return;
    if (!runDelete(
                "DELETE FROM channel_recipients WHERE channel_id IN (SELECT id FROM channels WHERE guild_id = :guild_id)",
                "GuildRepository: Delete guild recipients"))
        return;
    if (!runDelete("DELETE FROM roles WHERE guild_id = :guild_id",
                   "GuildRepository: Delete guild roles"))
        return;
    if (!runDelete("DELETE FROM channels WHERE guild_id = :guild_id",
                   "GuildRepository: Delete guild channels"))
        return;

    q.prepare("DELETE FROM guilds WHERE id = :id");
    q.bindValue(":id", static_cast<qint64>(guildId));
    execLogged(q, "GuildRepository: Delete");
}

std::optional<Discord::Guild> GuildRepository::getGuild(Core::Snowflake guildId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT id, name, icon, owner_id
        FROM guilds WHERE id = :id
    )");
    q.bindValue(":id", static_cast<qint64>(guildId));

    if (!q.exec() || !q.next())
        return std::nullopt;

    Discord::Guild guild;
    guild.id = static_cast<Core::Snowflake>(q.value(0).toLongLong());
    guild.name = q.value(1).toString();
    guild.icon = q.value(2).toString();
    guild.ownerId = static_cast<Core::Snowflake>(q.value(3).toLongLong());

    return guild;
}
} // namespace Storage
} // namespace Acheron

#include "ChannelRepository.hpp"

#include "DatabaseManager.hpp"
#include "Transaction.hpp"
#include "Core/Logging.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlError>

namespace Acheron {
namespace Storage {

static Discord::Channel readChannelFromQuery(const QSqlQuery &q)
{
    Discord::Channel channel;
    channel.id = static_cast<Core::Snowflake>(q.value(0).toLongLong());
    channel.type = static_cast<Discord::ChannelType>(q.value(1).toInt());
    if (!q.value(2).isNull())
        channel.position = q.value(2).toInt();
    if (!q.value(3).isNull())
        channel.name = q.value(3).toString();
    if (!q.value(4).isNull())
        channel.guildId = static_cast<Core::Snowflake>(q.value(4).toLongLong());
    if (!q.value(5).isNull())
        channel.parentId = static_cast<Core::Snowflake>(q.value(5).toLongLong());
    if (!q.value(6).isNull())
        channel.lastMessageId = static_cast<Core::Snowflake>(q.value(6).toLongLong());
    if (!q.value(7).isNull())
        channel.icon = q.value(7).toString();
    if (!q.value(8).isNull())
        channel.ownerId = static_cast<Core::Snowflake>(q.value(8).toLongLong());
    if (!q.value(9).isNull())
        channel.rateLimitPerUser = q.value(9).toInt();
    if (!q.value(10).isNull()) {
        channel.availableTagsJson = q.value(10).toString();
        QJsonDocument doc = QJsonDocument::fromJson(channel.availableTagsJson.toUtf8());
        if (doc.isArray()) {
            QList<Discord::Channel::ForumTag> tags;
            for (const QJsonValue &val : doc.array())
                tags.append(Discord::Channel::ForumTag::fromJson(val.toObject()));
            channel.availableTags = tags;
        }
    }
    if (!q.value(11).isNull())
        channel.defaultSortOrder = q.value(11).toInt();
    if (!q.value(12).isNull())
        channel.flags = Discord::ChannelFlags::fromInt(q.value(12).toInt());
    return channel;
}

static Discord::PermissionOverwrite readOverwrite(const QSqlQuery &q, int base)
{
    Discord::PermissionOverwrite ow;
    ow.id = static_cast<Core::Snowflake>(q.value(base).toLongLong());
    ow.type = static_cast<Discord::PermissionOverwrite::Type>(q.value(base + 1).toInt());
    ow.allow = Discord::Permissions::fromInt(q.value(base + 2).toLongLong());
    ow.deny = Discord::Permissions::fromInt(q.value(base + 3).toLongLong());
    return ow;
}

ChannelRepository::ChannelRepository(Core::Snowflake accountId)
    : BaseRepository(DatabaseManager::getCacheConnectionName(accountId))
{
}

bool ChannelRepository::saveChannel(const Discord::Channel &channel, QSqlDatabase &db)
{
    QSqlQuery q(db);

    q.prepare(R"(
		INSERT INTO channels
		(id, type, position, name, guild_id, parent_id, last_message_id, icon, owner_id, rate_limit_per_user, available_tags, default_sort_order, flags)
		VALUES (:id, :type, :position, :name, :guild_id, :parent_id, :last_message_id, :icon, :owner_id, :rate_limit_per_user, :available_tags, :default_sort_order, :flags)
		ON CONFLICT(id) DO UPDATE SET
		    type=excluded.type, position=excluded.position, name=excluded.name,
		    guild_id=excluded.guild_id, parent_id=excluded.parent_id,
		    last_message_id=excluded.last_message_id, icon=excluded.icon,
		    owner_id=excluded.owner_id,
		    -- Optional "configuration" columns: a payload that simply omits them
		    -- (READY guild channels, partial CHANNEL_UPDATEs) must not NULL-clobber
		    -- previously cached values (forum tags, slowmode, sort order, flags).
		    rate_limit_per_user=COALESCE(excluded.rate_limit_per_user, rate_limit_per_user),
		    available_tags=COALESCE(excluded.available_tags, available_tags),
		    default_sort_order=COALESCE(excluded.default_sort_order, default_sort_order),
		    flags=COALESCE(excluded.flags, flags)
    )");

    q.bindValue(":id", static_cast<qint64>(channel.id.get()));
    q.bindValue(":type", static_cast<qint64>(channel.type.get()));
    bindOptional(q, ":position", channel.position);
    bindOptional(q, ":name", channel.name);
    bindOptional(q, ":guild_id", channel.guildId);
    bindOptional(q, ":parent_id", channel.parentId);
    bindOptional(q, ":last_message_id", channel.lastMessageId);
    bindOptional(q, ":icon", channel.icon);
    bindOptional(q, ":owner_id", channel.ownerId);
    bindOptional(q, ":rate_limit_per_user", channel.rateLimitPerUser);
    q.bindValue(":available_tags",
                channel.availableTagsJson.isEmpty() ? QVariant() : channel.availableTagsJson);
    bindOptional(q, ":default_sort_order", channel.defaultSortOrder);
    q.bindValue(":flags", channel.flags.hasValue()
                                  ? QVariant(static_cast<qint64>(channel.flags.get()))
                                  : QVariant());

    return execLogged(q, "ChannelRepository: Save");
}

bool ChannelRepository::deleteChannel(Core::Snowflake channelId, QSqlDatabase &db)
{
    bool ownsTransaction = false;
    if (!Transaction::isActive(db.connectionName())) {
        if (!db.transaction()) {
            qCWarning(LogDB) << "ChannelRepository: failed to begin transaction:"
                             << db.lastError().text();
            return false;
        }
        ownsTransaction = true;
    }
    bool ok = false;
    do {
        QSqlQuery q(db);
        // Delete orphaned attachments/messages for this channel (no FK cascade).
        q.prepare("DELETE FROM attachments WHERE message_id IN (SELECT id FROM messages WHERE channel_id = :cid)");
        q.bindValue(":cid", static_cast<qint64>(channelId));
        if (!execLogged(q, "ChannelRepository: Delete orphan attachments"))
            break;

        q.prepare("DELETE FROM messages WHERE channel_id = :channel_id");
        q.bindValue(":channel_id", static_cast<qint64>(channelId));
        if (!execLogged(q, "ChannelRepository: Delete messages"))
            break;

        q.prepare("DELETE FROM permission_overwrites WHERE channel_id = :channel_id");
        q.bindValue(":channel_id", static_cast<qint64>(channelId));
        if (!execLogged(q, "ChannelRepository: Delete overwrites"))
            break;

        q.prepare("DELETE FROM channel_recipients WHERE channel_id = :channel_id");
        q.bindValue(":channel_id", static_cast<qint64>(channelId));
        if (!execLogged(q, "ChannelRepository: Delete recipients"))
            break;

        q.prepare("DELETE FROM channels WHERE id = :id");
        q.bindValue(":id", static_cast<qint64>(channelId));
        if (!execLogged(q, "ChannelRepository: Delete channel"))
            break;

        ok = true;
    } while (false);

    if (!ok) {
        if (ownsTransaction)
            db.rollback();
        return false;
    }

    if (ownsTransaction && !db.commit()) {
        qCWarning(LogDB) << "ChannelRepository: failed to commit transaction";
        db.rollback();
        return false;
    }
    return true;
}

bool ChannelRepository::savePermissionOverwrites(
        Core::Snowflake channelId, const QList<Discord::PermissionOverwrite> &overwrites,
                                              QSqlDatabase &db)
{
    bool ownsTransaction = false;
    if (!Transaction::isActive(db.connectionName())) {
        if (!db.transaction()) {
            qCWarning(LogDB) << "ChannelRepository: failed to begin transaction:"
                             << db.lastError().text();
            return false;
        }
        ownsTransaction = true;
    }
    bool ok = false;
    do {
        QSqlQuery delQ(db);
        delQ.prepare("DELETE FROM permission_overwrites WHERE channel_id = :channel_id");
        delQ.bindValue(":channel_id", static_cast<qint64>(channelId));
        if (!execLogged(delQ, "ChannelRepository: Delete overwrites"))
            break;

        if (overwrites.isEmpty()) {
            ok = true;
            break;
        }

        QSqlQuery q(db);
        q.prepare(R"(
            INSERT INTO permission_overwrites
            (channel_id, target_id, type, allow, deny)
            VALUES (:channel_id, :target_id, :type, :allow, :deny)
        )");

        ok = true;
        for (const auto &ow : overwrites) {
            q.bindValue(":channel_id", static_cast<qint64>(channelId));
            q.bindValue(":target_id", static_cast<qint64>(ow.id.get()));
            q.bindValue(":type", static_cast<int>(ow.type.get()));
            q.bindValue(":allow", static_cast<qint64>(ow.allow.get().toInt()));
            q.bindValue(":deny", static_cast<qint64>(ow.deny.get().toInt()));

            if (!execLogged(q, "ChannelRepository: Save overwrite"))
            {
                ok = false;
                break;
            }
        }
    } while (false);

    if (!ok) {
        if (ownsTransaction)
            db.rollback();
        return false;
    }

    if (ownsTransaction && !db.commit()) {
        qCWarning(LogDB) << "ChannelRepository: failed to commit transaction";
        db.rollback();
        return false;
    }
    return true;
}

bool ChannelRepository::saveChannelRecipients(Core::Snowflake channelId,
                                              const QList<Core::Snowflake> &recipientIds,
        QSqlDatabase &db)
{
    bool ownsTransaction = false;
    if (!Transaction::isActive(db.connectionName())) {
        if (!db.transaction()) {
            qCWarning(LogDB) << "ChannelRepository: failed to begin transaction:"
                             << db.lastError().text();
            return false;
        }
        ownsTransaction = true;
    }
    bool ok = false;
    do {
        QSqlQuery delQ(db);
        delQ.prepare("DELETE FROM channel_recipients WHERE channel_id = :channel_id");
        delQ.bindValue(":channel_id", static_cast<qint64>(channelId));
        if (!execLogged(delQ, "ChannelRepository: Delete recipients"))
            break;

        if (recipientIds.isEmpty()) {
            ok = true;
            break;
        }

        QSqlQuery q(db);
        q.prepare(R"(
            INSERT INTO channel_recipients
            (channel_id, user_id)
            VALUES (:channel_id, :user_id)
        )");

        ok = true;
        for (const auto &userId : recipientIds) {
            q.bindValue(":channel_id", static_cast<qint64>(channelId));
            q.bindValue(":user_id", static_cast<qint64>(userId));

            if (!execLogged(q, "ChannelRepository: Save recipient"))
            {
                ok = false;
                break;
            }
        }
    } while (false);

    if (!ok) {
        if (ownsTransaction)
            db.rollback();
        return false;
    }

    if (ownsTransaction && !db.commit()) {
        qCWarning(LogDB) << "ChannelRepository: failed to commit transaction";
        db.rollback();
        return false;
    }
    return true;
}

QList<Discord::PermissionOverwrite> ChannelRepository::getPermissionOverwrites(
        Core::Snowflake channelId)
{
    QList<Discord::PermissionOverwrite> overwrites;
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT target_id, type, allow, deny
        FROM permission_overwrites WHERE channel_id = :channel_id
    )");
    q.bindValue(":channel_id", static_cast<qint64>(channelId));

    if (!q.exec())
        return overwrites;

    while (q.next())
        overwrites.append(readOverwrite(q, 0));

    return overwrites;
}

QList<Core::Snowflake> ChannelRepository::getChannelRecipientIds(Core::Snowflake channelId)
{
    QList<Core::Snowflake> recipientIds;
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT user_id
        FROM channel_recipients WHERE channel_id = :channel_id
    )");
    q.bindValue(":channel_id", static_cast<qint64>(channelId));

    if (!q.exec())
        return recipientIds;

    while (q.next())
        recipientIds.append(static_cast<Core::Snowflake>(q.value(0).toLongLong()));

    return recipientIds;
}

QHash<Core::Snowflake, QList<Discord::PermissionOverwrite>> ChannelRepository::
        getPermissionOverwritesForGuild(Core::Snowflake guildId)
{
    QHash<Core::Snowflake, QList<Discord::PermissionOverwrite>> result;
    auto db = getDb();

    QSqlQuery channelIdsQuery(db);
    channelIdsQuery.prepare("SELECT id FROM channels WHERE guild_id = :guild_id");
    channelIdsQuery.bindValue(":guild_id", static_cast<qint64>(guildId));

    if (!channelIdsQuery.exec())
        return result;

    QList<Core::Snowflake> channelIds;
    while (channelIdsQuery.next())
        channelIds.append(static_cast<Core::Snowflake>(channelIdsQuery.value(0).toLongLong()));

    if (channelIds.isEmpty())
        return result;

    // Chunk IN clause to avoid SQLite 999 variable limit.
    constexpr int chunkSize = 500;
    for (int offset = 0; offset < channelIds.size(); offset += chunkSize) {
        const int count = qMin(chunkSize, channelIds.size() - offset);
        QString placeholders;
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                placeholders += ",";
            placeholders += QString(":id%1").arg(i);
        }

        QSqlQuery q(db);
        q.prepare(QString(R"(
            SELECT channel_id, target_id, type, allow, deny
            FROM permission_overwrites
            WHERE channel_id IN (%1)
        )").arg(placeholders));

        for (int i = 0; i < count; ++i)
            q.bindValue(QString(":id%1").arg(i), static_cast<qint64>(channelIds[offset + i]));

        if (!q.exec()) {
            qCWarning(LogDB) << "getPermissionOverwritesForGuild: chunk query failed" << q.lastError().text();
            continue;
        }

        while (q.next()) {
            Core::Snowflake channelId = static_cast<Core::Snowflake>(q.value(0).toLongLong());
            result[channelId].append(readOverwrite(q, 1));
        }
    }

    return result;
}

std::optional<Discord::Channel> ChannelRepository::getChannel(Core::Snowflake channelId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT id, type, position, name, guild_id, parent_id, last_message_id, icon, owner_id, rate_limit_per_user, available_tags, default_sort_order, flags
        FROM channels WHERE id = :id
    )");
    q.bindValue(":id", static_cast<qint64>(channelId));

    if (!q.exec() || !q.next())
        return std::nullopt;

    Discord::Channel channel = readChannelFromQuery(q);

    channel.permissionOverwrites = getPermissionOverwrites(channelId);

    if (channel.type == Discord::ChannelType::DM || channel.type == Discord::ChannelType::GROUP_DM)
        channel.recipientIds = getChannelRecipientIds(channelId);

    return channel;
}

std::optional<Core::Snowflake>
ChannelRepository::findDmChannelWithUser(Core::Snowflake userId)
{
    auto db = getDb();
    QSqlQuery q(db);
    // 1 = dm
    q.prepare(R"(
        SELECT c.id
        FROM channels c
        JOIN channel_recipients cr ON cr.channel_id = c.id
        WHERE c.type = 1 AND cr.user_id = :user_id
        LIMIT 1
    )");
    q.bindValue(":user_id", static_cast<qint64>(userId));

    if (!q.exec() || !q.next())
        return std::nullopt;

    return static_cast<Core::Snowflake>(q.value(0).toLongLong());
}

QList<Discord::Channel> ChannelRepository::getChannelsForGuild(Core::Snowflake guildId)
{
    QList<Discord::Channel> channels;
    auto db = getDb();
    QSqlQuery q(db);

    q.prepare(R"(
        SELECT id, type, position, name, guild_id, parent_id, last_message_id, icon, owner_id, rate_limit_per_user, available_tags, default_sort_order, flags
        FROM channels WHERE guild_id = :guild_id
    )");
    q.bindValue(":guild_id", static_cast<qint64>(guildId));

    if (!execLogged(q, "ChannelRepository: Get channels for guild"))
        return channels;

    while (q.next()) {
        // todo: permission overwrites not loaded because the caller doesnt need it rn
        channels.append(readChannelFromQuery(q));
    }

    return channels;
}

} // namespace Storage
} // namespace Acheron

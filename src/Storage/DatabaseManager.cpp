#include "DatabaseManager.hpp"

#include <QMutexLocker>
#include <QSettings>
#include <QDir>

#include "Core/Logging.hpp"

namespace Acheron {
namespace Storage {

namespace {
bool columnExists(QSqlDatabase &db, const QString &table, const QString &column)
{
    QSqlQuery query(db);
    if (!query.exec(QString("PRAGMA table_info(%1)").arg(table)))
        return false;

    while (query.next()) {
        if (query.value(1).toString() == column)
            return true;
    }
    return false;
}

int schemaVersion(QSqlDatabase &db)
{
    QSqlQuery query(db);
    if (!query.exec("PRAGMA user_version") || !query.next())
        return 0;
    return query.value(0).toInt();
}

void setSchemaVersion(QSqlDatabase &db, int version)
{
    QSqlQuery query(db);
    query.exec(QString("PRAGMA user_version = %1").arg(version));
}
} // namespace

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::init()
{
    QMutexLocker locker(&dbMutex);
    QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dirPath);
    if (!dir.exists())
        dir.mkpath(".");

    persistentPath = dir.filePath("acheron.sqlite");
    persistentDb = QSqlDatabase::addDatabase("QSQLITE", PERSISTENT_CONN_NAME);
    persistentDb.setDatabaseName(persistentPath);

    if (!persistentDb.open()) {
        qCCritical(LogDB) << "Persistent DB init failed:" << persistentDb.lastError().text();
        return false;
    }

    {
        QSqlQuery config(persistentDb);
        config.exec("PRAGMA foreign_keys = ON");
    }

    setupPersistentTables();
    return true;
}

void DatabaseManager::shutdown()
{
    QMutexLocker locker(&dbMutex);
    if (persistentDb.isOpen())
        persistentDb.close();
    persistentDb = QSqlDatabase();
    QSqlDatabase::removeDatabase(PERSISTENT_CONN_NAME);
}

QString DatabaseManager::openCacheDatabase(Core::Snowflake accountId)
{
    QMutexLocker locker(&dbMutex);
    QString connName = getCacheConnectionName(accountId);

    if (QSqlDatabase::contains(connName))
        return connName;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);

    bool inMemory = QSettings().value("general/in_memory_cache", false).toBool();
    if (inMemory) {
        db.setDatabaseName(":memory:");
    } else {
        QString dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QString dbPath = QDir(dirPath).filePath(QString("cache_%1.sqlite").arg(accountId.toString()));

        db.setDatabaseName(dbPath);
    }

    if (!db.open()) {
        qCCritical(LogDB) << "Cache DB init failed:" << db.lastError().text();
        return "";
    }

    {
        QSqlQuery config(db);
        config.exec("PRAGMA foreign_keys = ON");
        if (!inMemory) {
            config.exec("PRAGMA journal_mode = WAL");
            config.exec("PRAGMA synchronous = OFF");
        }
    }

    setupCacheTables(connName);

    return connName;
}

void DatabaseManager::closeCacheDatabase(Core::Snowflake accountId)
{
    QMutexLocker locker(&dbMutex);
    QString connName = getCacheConnectionName(accountId);

    if (QSqlDatabase::contains(connName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(connName);
            if (db.isOpen())
                db.close();
        }
        QSqlDatabase::removeDatabase(connName);
    }
}

QString DatabaseManager::getCacheConnectionName(Core::Snowflake accountId)
{
    return QString("Acheron_Cache_%1").arg(accountId.toString());
}

void DatabaseManager::setupPersistentTables()
{
    QSqlDatabase db = QSqlDatabase::database(PERSISTENT_CONN_NAME);
    QSqlQuery query(db);

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS accounts (
            id INTEGER PRIMARY KEY,
            username TEXT,
            display_name TEXT,
            avatar TEXT,
            gateway_url TEXT,
            rest_url TEXT,
            cdn_url TEXT,
            display_order INTEGER DEFAULT 0,
            auto_connect INTEGER NOT NULL DEFAULT 0
        )
    )");

    applyPersistentMigrations(db);
}

void DatabaseManager::applyPersistentMigrations(QSqlDatabase &db)
{
    constexpr int latestSchemaVersion = 1;
    int version = schemaVersion(db);
    if (version >= latestSchemaVersion)
        return;

    QSqlQuery query(db);

    // Migration 1: accounts.auto_connect
    if (version < 1 && !columnExists(db, "accounts", "auto_connect"))
        query.exec("ALTER TABLE accounts ADD COLUMN auto_connect INTEGER NOT NULL DEFAULT 0");

    setSchemaVersion(db, latestSchemaVersion);
}

void DatabaseManager::setupCacheTables(const QString &connName)
{
    QSqlDatabase db = QSqlDatabase::database(connName);
    QSqlQuery query(db);

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "guilds" (
	        "id" INTEGER NOT NULL,
	        "name" TEXT NOT NULL,
	        "icon" TEXT,
	        "owner_id" INTEGER NOT NULL,
	        PRIMARY KEY("id")
        )
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "channels" (
	        "id" INTEGER NOT NULL,
	        "type" INTEGER NOT NULL,
	        "position" INTEGER,
	        "name" TEXT,
	        "guild_id" INTEGER,
	        "parent_id" INTEGER,
	        "last_message_id" INTEGER,
	        "icon" TEXT,
	        "owner_id" INTEGER,
	        "rate_limit_per_user" INTEGER,
	        "available_tags" TEXT,
	        "default_sort_order" INTEGER,
	        "flags" INTEGER,
	        PRIMARY KEY("id")
        );
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "messages" (
	        "id" INTEGER NOT NULL,
	        "author_id" INTEGER NOT NULL,
	        "channel_id" INTEGER NOT NULL,
	        "content" TEXT NOT NULL,
	        "timestamp" TEXT NOT NULL,
	        "edited_timestamp" TEXT,
	        "type" INTEGER NOT NULL,
	        "flags" INTEGER NOT NULL,
	        "embeds" TEXT,
	        "reactions" TEXT,
	        "deleted" INTEGER NOT NULL,
	        "referenced_message_id" INTEGER,
	        "context_only" INTEGER NOT NULL DEFAULT 0,
	        PRIMARY KEY("id")
        );
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "users" (
	        "id" INTEGER NOT NULL,
	        "username" TEXT NOT NULL,
	        "global_name" TEXT,
	        "avatar" TEXT,
	        "bot" INTEGER,
            PRIMARY KEY("id")
        );
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "members" (
            "user_id" INTEGER NOT NULL,
            "guild_id" INTEGER NOT NULL,
            "nick" TEXT,
            "avatar" TEXT,
            "roles" TEXT,
            "joined_at" TEXT,
            "premium_since" TEXT,
            "deaf" INTEGER,
            "mute" INTEGER,
            "flags" INTEGER,
            "pending" INTEGER,
            "communication_disabled_until" TEXT,
            PRIMARY KEY("user_id", "guild_id")
        );
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "attachments" (
            "id" INTEGER NOT NULL,
            "message_id" INTEGER NOT NULL,
            "filename" TEXT NOT NULL,
            "content_type" TEXT,
            "size" INTEGER NOT NULL,
            "url" TEXT NOT NULL,
            "proxy_url" TEXT NOT NULL,
            "width" INTEGER,
            "height" INTEGER,
            PRIMARY KEY("id")
        );
    )");

    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_channel ON messages(channel_id, id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_ref_msg ON messages(referenced_message_id);");

    query.exec("CREATE INDEX IF NOT EXISTS idx_attachments_message_id ON attachments(message_id);");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "roles" (
            "id" INTEGER NOT NULL,
            "guild_id" INTEGER NOT NULL,
            "name" TEXT NOT NULL,
            "permissions" INTEGER NOT NULL,
            "position" INTEGER NOT NULL,
            "color" INTEGER,
            "hoist" INTEGER,
            "icon" TEXT,
            "unicode_emoji" TEXT,
            "managed" INTEGER,
            "mentionable" INTEGER,
            PRIMARY KEY("id", "guild_id"),
            FOREIGN KEY("guild_id") REFERENCES guilds("id") ON DELETE CASCADE
        );
    )");

    query.exec("CREATE INDEX IF NOT EXISTS idx_roles_guild_id ON roles(guild_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_roles_position ON roles(guild_id, position);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_roles_lookup ON roles(guild_id, id);");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "permission_overwrites" (
            "channel_id" INTEGER NOT NULL,
            "target_id" INTEGER NOT NULL,
            "type" INTEGER NOT NULL,
            "allow" INTEGER NOT NULL,
            "deny" INTEGER NOT NULL,
            PRIMARY KEY("channel_id", "target_id"),
            FOREIGN KEY("channel_id") REFERENCES channels("id") ON DELETE CASCADE
        );
    )");

    query.exec("CREATE INDEX IF NOT EXISTS idx_overwrites_channel_id ON permission_overwrites(channel_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_members_lookup ON members(guild_id, user_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_channels_guild ON channels(guild_id);");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS "channel_recipients" (
            "channel_id" INTEGER NOT NULL,
            "user_id" INTEGER NOT NULL,
            PRIMARY KEY("channel_id", "user_id"),
            FOREIGN KEY("channel_id") REFERENCES channels("id") ON DELETE CASCADE,
            FOREIGN KEY("user_id") REFERENCES users("id")
        );
    )");

    query.exec("CREATE INDEX IF NOT EXISTS idx_channel_recipients_channel ON channel_recipients(channel_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_channel_recipients_user ON channel_recipients(user_id);");
}
} // namespace Storage
} // namespace Acheron

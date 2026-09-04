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
    // Table is an identifier: quote it to prevent SQL injection and avoid
    // unquoted interpolation (still only called with literal table names).
    QString safeTable = table;
    safeTable.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (!query.exec(QStringLiteral("PRAGMA table_info(\"%1\")").arg(safeTable)))
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

// Checkpoint, close, and removeDatabase() every registered connection whose
// name starts with \a namePrefix. Used both at process shutdown and when a
// single cache DB is closed so that per-thread clones (BaseRepository::getDb,
// named "<conn>_t<tid>") never outlive the primary connection: an orphaned
// clone keeps its file/WAL handle open (fd leak; on Windows the cache file
// stays locked against deletion) and, for shared-cache in-memory DBs, pins the
// old cache in memory across a logout/login of the same account.
// Only safe to call once the worker threads that used the clones have stopped
// (same precondition as the process-wide shutdown below).
static void closeAndRemoveConnections(const QString &namePrefix)
{
    const QStringList names = QSqlDatabase::connectionNames();
    for (const QString &name : names) {
        if (!name.startsWith(namePrefix))
            continue;
        {
            QSqlDatabase db = QSqlDatabase::database(name);
            if (db.isOpen()) {
                QSqlQuery(db).exec("PRAGMA wal_checkpoint(TRUNCATE)");
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(name);
    }
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
    creationThread_ = QThread::currentThread();
    QSqlDatabase persistentDb = QSqlDatabase::addDatabase("QSQLITE", PERSISTENT_CONN_NAME);
    persistentDb.setDatabaseName(persistentPath);

    if (!persistentDb.open()) {
        qCCritical(LogDB) << "Persistent DB init failed:" << persistentDb.lastError().text();
        return false;
    }

    {
        QSqlQuery config(persistentDb);
        if (!config.exec("PRAGMA foreign_keys = ON"))
            qCWarning(LogDB) << "PRAGMA foreign_keys ON failed:" << config.lastError().text();
        if (!config.exec("PRAGMA journal_mode = WAL"))
            qCWarning(LogDB) << "PRAGMA journal_mode=WAL failed:" << config.lastError().text();
        else {
            // Verify WAL actually enabled (fails on :memory:, RO FS, network share)
            if (config.exec("PRAGMA journal_mode") && config.next()) {
                QString mode = config.value(0).toString().toLower();
                if (mode != "wal")
                    qCWarning(LogDB) << "journal_mode not wal, got" << mode;
            }
        }
        if (!config.exec("PRAGMA synchronous = NORMAL"))
            qCWarning(LogDB) << "PRAGMA synchronous failed:" << config.lastError().text();
        if (!config.exec("PRAGMA busy_timeout = 5000"))
            qCWarning(LogDB) << "PRAGMA busy_timeout failed:" << config.lastError().text();
        // Verify foreign_keys
        if (config.exec("PRAGMA foreign_keys") && config.next() && config.value(0).toInt() != 1)
            qCWarning(LogDB) << "foreign_keys not enabled";
    }
    // Integrity check on startup (detect SQLITE_CORRUPT from prior crash)
    {
        QSqlDatabase db = QSqlDatabase::database(PERSISTENT_CONN_NAME);
        QSqlQuery q(db);
        if (q.exec("PRAGMA integrity_check") && q.next()) {
            QString result = q.value(0).toString().toLower();
            if (result != "ok")
                qCCritical(LogDB) << "Persistent DB integrity_check failed:" << result;
        }
    }

    setupPersistentTables();
    return true;
}

QThread *DatabaseManager::creationThread()
{
    return instance().creationThread_;
}

void DatabaseManager::shutdown()
{
    QMutexLocker locker(&dbMutex);
    // Checkpoint and close all cache connections first (avoid WAL orphan + fd leak).
    // Worker-thread clones (named "..._t<tid>") are closed here too: by the time
    // shutdown() runs, gateway/ingest threads have been joined, so no live worker
    // can be using them.
    closeAndRemoveConnections(QLatin1String("Acheron_Cache_"));
    // Persistent connection clones (file-based) were previously never closed,
    // leaking the fd + WAL handles per thread. Close them here as well (the
    // main persistent connection is handled below, so skip it here). The prefix
    // "Acheron_Persistent_t" selects the clones but not the primary connection.
    closeAndRemoveConnections(QLatin1String("Acheron_Persistent_t"));
    {
        QSqlDatabase db = QSqlDatabase::database(PERSISTENT_CONN_NAME);
        if (db.isValid() && db.isOpen()) {
            QSqlQuery(db).exec("PRAGMA wal_checkpoint(TRUNCATE)");
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(PERSISTENT_CONN_NAME);
}

QString DatabaseManager::openCacheDatabase(Core::Snowflake accountId)
{
    QMutexLocker locker(&dbMutex);
    QString connName = getCacheConnectionName(accountId);

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase existing = QSqlDatabase::database(connName);
        if (existing.isOpen() && existing.isValid())
            return connName;
        // Stale handle (closed by external code / race) — drop and recreate.
        QSqlDatabase::removeDatabase(connName);
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);

    bool inMemory = QSettings().value("general/in_memory_cache", false).toBool();
    if (inMemory) {
        // Shared-cache memory database: every connection to the SAME URI name
        // sees the same in-memory DB. Plain ":memory:" would give each
        // per-thread clone its own private empty database (writes from worker
        // threads would go to a DB nobody reads).
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI"));
        db.setDatabaseName(QStringLiteral("file:acheron_cache_%1?mode=memory&cache=shared")
                                   .arg(accountId.toString()));
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
            config.exec("PRAGMA synchronous = NORMAL");
            config.exec("PRAGMA busy_timeout = 5000");
        }
    }

    setupCacheTables(connName);
    {
        QSqlDatabase db = QSqlDatabase::database(connName);
        applyCacheMigrations(db);
    }

    return connName;
}

void DatabaseManager::closeCacheDatabase(Core::Snowflake accountId)
{
    QMutexLocker locker(&dbMutex);
    const QString connName = getCacheConnectionName(accountId);

    // Tear down per-thread clones first: BaseRepository::getDb() creates
    // "<conn>_t<tid>" connections on first access from a non-owning thread,
    // and those would otherwise keep the cache file/WAL handle open after
    // logout (fd + WAL leak, and on Windows the cache file stays locked
    // against deletion). Safe once the owning client instance has stopped.
    closeAndRemoveConnections(connName + QLatin1String("_t"));

    if (QSqlDatabase::contains(connName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(connName);
            if (db.isOpen()) {
                QSqlQuery(db).exec("PRAGMA wal_checkpoint(TRUNCATE)");
                db.close();
            }
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

    auto execChecked = [&](const QString &sql, const char *ctx) {
        if (!query.exec(sql))
            qCWarning(LogDB) << ctx << "failed:" << query.lastError().text();
    };

    execChecked(R"(
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
    )",
                "setupPersistentTables: accounts");

    applyPersistentMigrations(db);
}

void DatabaseManager::applyPersistentMigrations(QSqlDatabase &db)
{
    constexpr int latestSchemaVersion = 1;
    int version = schemaVersion(db);
    if (version >= latestSchemaVersion)
        return;

    QSqlQuery query(db);
    bool ok = true;

    // Migration 1: accounts.auto_connect
    if (version < 1 && !columnExists(db, "accounts", "auto_connect")) {
        if (!query.exec("ALTER TABLE accounts ADD COLUMN auto_connect INTEGER NOT NULL DEFAULT 0")) {
            qCWarning(LogDB) << "Migration 1 failed:" << query.lastError().text();
            ok = false;
        }
    }

    if (ok)
        setSchemaVersion(db, latestSchemaVersion);
}

void DatabaseManager::setupCacheTables(const QString &connName)
{
    QSqlDatabase db = QSqlDatabase::database(connName);
    QSqlQuery query(db);

    auto execLogged = [&](const QString &sql) {
        if (!query.exec(sql))
            qCWarning(LogDB) << "setupCacheTables failed:" << query.lastError().text() << sql.left(80);
    };

    execLogged(R"(
        CREATE TABLE IF NOT EXISTS "guilds" (
	        "id" INTEGER NOT NULL,
	        "name" TEXT NOT NULL,
	        "icon" TEXT,
	        "owner_id" INTEGER NOT NULL,
	        PRIMARY KEY("id")
        )
    )");

    execLogged(R"(
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

    execLogged(R"(
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
	        "parsed_content" TEXT,
	        PRIMARY KEY("id")
        );
    )");

    execLogged(R"(
        CREATE TABLE IF NOT EXISTS "users" (
	        "id" INTEGER NOT NULL,
	        "username" TEXT NOT NULL,
	        "global_name" TEXT,
	        "avatar" TEXT,
	        "bot" INTEGER,
            PRIMARY KEY("id")
        );
    )");

    execLogged(R"(
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

    execLogged(R"(
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

    execLogged("CREATE INDEX IF NOT EXISTS idx_messages_channel ON messages(channel_id, id);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_messages_ref_msg ON messages(referenced_message_id);");

    execLogged("CREATE INDEX IF NOT EXISTS idx_attachments_message_id ON attachments(message_id);");

    execLogged(R"(
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

    execLogged("CREATE INDEX IF NOT EXISTS idx_roles_guild_id ON roles(guild_id);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_roles_position ON roles(guild_id, position);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_roles_lookup ON roles(guild_id, id);");

    execLogged(R"(
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

    execLogged("CREATE INDEX IF NOT EXISTS idx_overwrites_channel_id ON permission_overwrites(channel_id);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_members_lookup ON members(guild_id, user_id);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_members_guild_joined ON members(guild_id, joined_at);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_channels_guild ON channels(guild_id);");

    execLogged(R"(
        CREATE TABLE IF NOT EXISTS "channel_recipients" (
            "channel_id" INTEGER NOT NULL,
            "user_id" INTEGER NOT NULL,
            PRIMARY KEY("channel_id", "user_id"),
            FOREIGN KEY("channel_id") REFERENCES channels("id") ON DELETE CASCADE,
            FOREIGN KEY("user_id") REFERENCES users("id")
        );
    )");

    execLogged("CREATE INDEX IF NOT EXISTS idx_channel_recipients_channel ON channel_recipients(channel_id);");
    execLogged("CREATE INDEX IF NOT EXISTS idx_channel_recipients_user ON channel_recipients(user_id);");
}

void DatabaseManager::applyCacheMigrations(QSqlDatabase &db)
{
    // The per-account cache DB previously had no version stamp — every schema
    // change was "CREATE TABLE IF NOT EXISTS", which silently skipped new
    // columns/tables on existing installs. Stamp a version now so future
    // migrations can be applied incrementally.
    constexpr int latestCacheSchemaVersion = 2;
    const int version = schemaVersion(db);
    if (version >= latestCacheSchemaVersion)
        return;

    QSqlQuery query(db);

    // Migration 2: persist the rendered markdown (`parsed_content`) so a disk
    // reload doesn't re-parse every message on the UI thread. Fresh databases
    // already get the column from CREATE TABLE, so only add it when missing.
    bool migrateOk = true;
    if (version < 2) {
        QSqlQuery check(db);
        check.exec("PRAGMA table_info(messages)");
        bool hasParsed = false;
        while (check.next()) {
            if (check.value(1).toString() == QLatin1String("parsed_content")) {
                hasParsed = true;
                break;
            }
        }
        if (!hasParsed) {
            if (!query.exec("ALTER TABLE messages ADD COLUMN parsed_content TEXT")) {
                qCWarning(LogDB) << "Migration 2 failed:" << query.lastError().text();
                migrateOk = false;
            }
        }
    }

    if (migrateOk)
        setSchemaVersion(db, latestCacheSchemaVersion);
}

} // namespace Storage
} // namespace Acheron

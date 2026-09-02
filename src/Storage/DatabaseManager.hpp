#pragma once

#include <QMutex>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QThread>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Storage {
class DatabaseManager
{
public:
    static constexpr char const *PERSISTENT_CONN_NAME = "Acheron_Persistent";
    static constexpr char const *CACHE_CONN_NAME = "Acheron_Cache";

    static DatabaseManager &instance();

    bool init();
    void shutdown();

    QString openCacheDatabase(Core::Snowflake accountId);
    void closeCacheDatabase(Core::Snowflake accountId);
    static QString getCacheConnectionName(Core::Snowflake accountId);

    // Thread that owns the connections created by init()/openCacheDatabase().
    // Repositories use this to decide whether a call arrives from the owning
    // thread (safe to reuse the shared handle) or a worker thread (must clone
    // a per-thread connection — QSqlDatabase handles are thread-affine).
    static QThread *creationThread();

private:
    void setupPersistentTables();
    void setupCacheTables(const QString &connName);
    void applyPersistentMigrations(QSqlDatabase &db);
    void applyCacheMigrations(QSqlDatabase &db);

    QString persistentPath;
    QThread *creationThread_ = nullptr;
    QMutex dbMutex;
};
} // namespace Storage
} // namespace Acheron
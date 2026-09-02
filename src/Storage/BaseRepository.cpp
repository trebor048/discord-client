#include "BaseRepository.hpp"
#include "DatabaseManager.hpp"

#include "Core/Logging.hpp"

#include <QSqlError>
#include <QThread>

namespace Acheron {
namespace Storage {
BaseRepository::BaseRepository(const QString &connName) : connName(connName) {}

QSqlDatabase BaseRepository::getDb() const
{
    // QSqlDatabase connections are thread-affine (Qt docs): a handle opened on
    // one thread must not be used from another (concurrent SQLite statements on
    // a single sqlite3* is UB and corrupts the database). The cache/persistent
    // connections are created on the main thread (DatabaseManager::init /
    // openCacheDatabase); a call from any OTHER thread must use a per-thread
    // clone instead of the shared handle.
    const bool onCreatorThread = QThread::currentThread() == DatabaseManager::creationThread();
    if (onCreatorThread) {
        QSqlDatabase db = QSqlDatabase::database(connName, false);
        if (db.isValid() && db.isOpen())
            return db;
    }

    // Per-thread clone (reuse an existing one for this thread if present).
    QString threadConn = connName + QStringLiteral("_t") + QString::number(reinterpret_cast<quint64>(QThread::currentThreadId()), 16);
    if (QSqlDatabase::contains(threadConn)) {
        QSqlDatabase tdb = QSqlDatabase::database(threadConn, false);
        if (tdb.isValid() && tdb.isOpen())
            return tdb;
    }

    // Resolve the underlying database file/URI from any existing connection.
    QString dbName;
    const auto all = QSqlDatabase::connectionNames();
    for (const auto &n : all) {
        if (n == connName || n.startsWith(connName + "_t")) {
            QSqlDatabase cand = QSqlDatabase::database(n, false);
            if (cand.isValid()) {
                dbName = cand.databaseName();
                if (!dbName.isEmpty())
                    break;
            }
        }
    }
    if (dbName.isEmpty()) {
        // Last resort: return invalid (caller will log)
        return QSqlDatabase::database(connName, false);
    }

    QSqlDatabase clone = QSqlDatabase::addDatabase("QSQLITE", threadConn);
    clone.setDatabaseName(dbName);
    // Shared-cache in-memory DBs are addressed by URI; the clone must open the
    // same named memory DB, which requires URI filenames on this connection too.
    if (dbName.startsWith(QLatin1String("file:")))
        clone.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI"));
    if (!clone.open()) {
        qCWarning(LogDB) << "BaseRepository: thread-local clone open failed for" << threadConn << clone.lastError().text();
        return QSqlDatabase::database(connName, false);
    }
    // Ensure FK/WAL settings for clone (SQLite pragmas are per-connection)
    QSqlQuery(clone).exec("PRAGMA foreign_keys = ON");
    QSqlQuery(clone).exec("PRAGMA busy_timeout = 5000");
    return clone;
}

bool BaseRepository::execLogged(QSqlQuery &q, const char *context)
{
    if (q.exec())
        return true;

    qCWarning(LogDB) << context << "failed:" << q.lastError().text();
    return false;
}

} // namespace Storage
} // namespace Acheron

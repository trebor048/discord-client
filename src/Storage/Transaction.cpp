#include "Transaction.hpp"

#include "Core/Logging.hpp"

#include <QMutex>
#include <QSet>
#include <QSqlError>

namespace Acheron {
namespace Storage {

namespace {
QMutex gActiveTransactionsMutex;
QSet<QString> activeTransactions;
} // namespace

Transaction::Transaction(QSqlDatabase &db)
    : db(db),
      connName(db.connectionName())
{
    // Hold the mutex across the whole check-then-act so two threads sharing a
    // connection name cannot both BEGIN (SQLite errors with "cannot start a
    // transaction within a transaction").
    QMutexLocker lock(&gActiveTransactionsMutex);
    if (activeTransactions.contains(connName)) {
        // Nested transaction: do not BEGIN; owns stays false and commit()/dtor
        // are no-ops. Callers (repositories) check isActive() first, so they
        // already know an outer transaction owns the connection.
        return;
    }

    if (!db.transaction()) {
        qCWarning(LogDB) << "Failed to begin transaction on" << connName << ":" << db.lastError().text();
        return;
    }

    activeTransactions.insert(connName);
    owns = true;
}

Transaction::~Transaction()
{
    if (!owns || finished)
        return;

    db.rollback();
    QMutexLocker lock(&gActiveTransactionsMutex);
    activeTransactions.remove(connName);
}

bool Transaction::isActive(const QString &connName)
{
    QMutexLocker lock(&gActiveTransactionsMutex);
    return activeTransactions.contains(connName);
}

bool Transaction::commit()
{
    if (!owns || finished)
        return true;

    finished = true;

    bool ok = db.commit();
    {
        QMutexLocker lock(&gActiveTransactionsMutex);
        activeTransactions.remove(connName);
    }
    if (!ok) {
        qCWarning(LogDB) << "Failed to commit transaction on" << connName << ":" << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

} // namespace Storage
} // namespace Acheron

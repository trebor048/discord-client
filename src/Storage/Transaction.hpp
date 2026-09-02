#pragma once

#include <QSqlDatabase>
#include <QString>

namespace Acheron {
namespace Storage {

/// RAII database transaction helper.
///
/// Begins a transaction on construction (if one isn't already active on this
/// connection), and automatically rolls back on destruction if commit() was
/// never called.  Thread-safe via a global mutex-protected set of active connections.
///
/// Usage:
/// @code
///   Storage::Transaction txn(db);
///   // ... perform database operations ...
///   txn.commit();
///   // if commit() is not called, rollback happens in ~Transaction
/// @endcode
class Transaction
{
public:
    explicit Transaction(QSqlDatabase &db);
    ~Transaction();

    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;

    /// Commit the transaction.  Returns false if commit fails (rolls back automatically).
    /// Once called, the destructor is a no-op.
    bool commit();

    /// Returns true if this object owns an active transaction.
    bool ownsTransaction() const { return owns; }

    /// Returns true if a Transaction currently owns a transaction on this connection.
    static bool isActive(const QString &connName);

private:
    QSqlDatabase db;
    QString connName;
    bool owns = false;
    bool finished = false;
};

} // namespace Storage
} // namespace Acheron

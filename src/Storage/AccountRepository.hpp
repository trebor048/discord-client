#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>

#include "BaseRepository.hpp"
#include "DatabaseManager.hpp"
#include "Core/AccountInfo.hpp"

namespace Acheron {
namespace Storage {

class AccountRepository : public BaseRepository
{
public:
    AccountRepository();

    /// Persists \a acc. Returns false (after logging) when the row could not be
    /// written, so callers can roll back the keychain credential they stored
    /// first instead of leaving a token with no account row.
    bool saveAccount(const Core::AccountInfo &acc);
    Core::AccountInfo getAccount(quint64 id);
    QVector<Core::AccountInfo> getAllAccounts();
    void removeAccount(quint64 id);
    void updateDisplayOrder(quint64 id, int order);
    void updateAutoConnect(quint64 id, bool autoConnect);
};

} // namespace Storage
} // namespace Acheron

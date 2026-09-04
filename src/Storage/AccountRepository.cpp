#include "AccountRepository.hpp"

#include "Core/Logging.hpp"
#include "Core/TokenStore.hpp"

namespace Acheron {
namespace Storage {

AccountRepository::AccountRepository()
    : BaseRepository(DatabaseManager::PERSISTENT_CONN_NAME)
{
}

bool AccountRepository::saveAccount(const Core::AccountInfo &acc)
{
    auto db = getDb();
    if (!db.isOpen()) {
        qCWarning(LogDB) << "AccountRepository: Persistent DB not open!";
        return false;
    }

    QSqlQuery query(db);
    query.prepare(R"(
            INSERT OR REPLACE INTO accounts
            (id, username, display_name, avatar, gateway_url, rest_url, cdn_url, display_order, auto_connect)
            VALUES (:id, :username, :display_name, :avatar, :gateway_url, :rest_url, :cdn_url, :display_order, :auto_connect)
        )");

    query.bindValue(":id", static_cast<qint64>(acc.id));
    query.bindValue(":username", acc.username);
    query.bindValue(":display_name", acc.displayName);
    query.bindValue(":avatar", acc.avatar);
    query.bindValue(":gateway_url", acc.gatewayUrl);
    query.bindValue(":rest_url", acc.restUrl);
    query.bindValue(":cdn_url", acc.cdnUrl);
    query.bindValue(":display_order", acc.displayOrder);
    query.bindValue(":auto_connect", acc.autoConnect ? 1 : 0);

    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Save failed:" << query.lastError().text();
        return false;
    }
    return true;
}

Core::AccountInfo AccountRepository::getAccount(quint64 id)
{
    auto db = getDb();
    if (!db.isOpen()) {
        qCWarning(LogDB) << "AccountRepository: Persistent DB not open!";
        return {};
    }

    QSqlQuery query(db);

    query.prepare(R"(
        SELECT id, username, display_name, avatar, gateway_url, rest_url, cdn_url, display_order, auto_connect
        FROM accounts WHERE id = :id
    )");

    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Get failed:" << query.lastError().text();
        return {};
    }

    if (!query.next()) {
        qCWarning(LogDB) << "AccountRepository: Account not found";
        return {};
    }

    Core::AccountInfo acc;

    acc.id = static_cast<Core::Snowflake>(query.value(0).toLongLong());
    acc.username = query.value(1).toString();
    acc.displayName = query.value(2).toString();
    acc.avatar = query.value(3).toString();
    acc.gatewayUrl = query.value(4).toString();
    acc.restUrl = query.value(5).toString();
    acc.cdnUrl = query.value(6).toString();
    acc.displayOrder = query.value(7).toInt();
    acc.autoConnect = query.value(8).toBool();

    return acc;
}

QVector<Core::AccountInfo> AccountRepository::getAllAccounts()
{
    QVector<Core::AccountInfo> results;
    auto db = getDb();

    if (!db.isOpen())
        return results;

    QSqlQuery query(R"(
        SELECT id, username, display_name, avatar, gateway_url, rest_url, cdn_url, display_order, auto_connect
        FROM accounts ORDER BY display_order ASC
    )", db);

    if (query.lastError().isValid())
        qCWarning(LogDB) << "AccountRepository: getAllAccounts failed:" << query.lastError().text();

    while (query.next()) {
        Core::AccountInfo acc;

        acc.id = static_cast<quint64>(query.value(0).toLongLong());

        acc.username = query.value(1).toString();
        acc.displayName = query.value(2).toString();
        acc.avatar = query.value(3).toString();
        acc.gatewayUrl = query.value(4).toString();
        acc.restUrl = query.value(5).toString();
        acc.cdnUrl = query.value(6).toString();
        acc.displayOrder = query.value(7).toInt();
        acc.autoConnect = query.value(8).toBool();

        acc.state = Core::ConnectionState::Disconnected;

        results.append(acc);
    }

    return results;
}

void AccountRepository::removeAccount(quint64 id)
{
    auto db = getDb();
    if (!db.isOpen()) {
        qCWarning(LogDB) << "AccountRepository: Persistent DB not open!";
        return;
    }

    QSqlQuery query(db);

    query.prepare("DELETE FROM accounts WHERE id = :id");
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec()) {
        qCWarning(LogDB) << "AccountRepository: Remove failed:" << query.lastError().text();
        return;
    }

    // Delete the DB row first on purpose: if the keychain delete below fails we
    // may orphan a credential, but the reverse order risks a worse state (a row
    // that still exists with its token already gone) whenever the token delete
    // fails while the row delete would have succeeded.
    if (!Core::TokenStore::deleteToken(Core::Snowflake(id)))
        qCWarning(LogDB) << "AccountRepository: Failed to delete token for removed account"
                         << id << "; credential may be orphaned in the keychain";
}

void AccountRepository::updateDisplayOrder(quint64 id, int order)
{
    auto db = getDb();
    if (!db.isOpen()) {
        qCWarning(LogDB) << "AccountRepository: Persistent DB not open!";
        return;
    }

    QSqlQuery query(db);

    query.prepare("UPDATE accounts SET display_order = :order WHERE id = :id");
    query.bindValue(":order", order);
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Update display order failed:"
                         << query.lastError().text();
}

void AccountRepository::updateAutoConnect(quint64 id, bool autoConnect)
{
    auto db = getDb();
    if (!db.isOpen()) {
        qCWarning(LogDB) << "AccountRepository: Persistent DB not open!";
        return;
    }

    QSqlQuery query(db);

    query.prepare("UPDATE accounts SET auto_connect = :auto_connect WHERE id = :id");
    query.bindValue(":auto_connect", autoConnect ? 1 : 0);
    query.bindValue(":id", static_cast<qint64>(id));

    if (!query.exec())
        qCWarning(LogDB) << "AccountRepository: Update auto_connect failed:"
                         << query.lastError().text();
}

} // namespace Storage
} // namespace Acheron

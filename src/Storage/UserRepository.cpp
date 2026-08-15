#include "UserRepository.hpp"

#include "DatabaseManager.hpp"
#include "Transaction.hpp"
#include "Core/Logging.hpp"

namespace Acheron {
namespace Storage {

UserRepository::UserRepository(Core::Snowflake accountId)
    : BaseRepository(DatabaseManager::getCacheConnectionName(accountId))
{
}

bool UserRepository::saveUser(const Discord::User &user)
{
    auto db = getDb();
    return saveUser(user, db);
}

bool UserRepository::saveUser(const Discord::User &user, QSqlDatabase &db)
{
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT OR REPLACE INTO users
        (id, username, global_name, avatar, bot)
        VALUES (:id, :username, :global_name, :avatar, :bot)
    )");

    q.bindValue(":id", static_cast<qint64>(user.id.get()));
    q.bindValue(":username", user.username);
    bindOptional(q, ":global_name", user.globalName);
    bindOptional(q, ":avatar", user.avatar);
    bindOptional(q, ":bot", user.bot);

    return execLogged(q, "UserRepository: Save user");
}

bool UserRepository::saveUsers(const QList<Discord::User> &users)
{
    auto db = getDb();
    return saveUsers(users, db);
}

bool UserRepository::saveUsers(const QList<Discord::User> &users, QSqlDatabase &db)
{
    if (users.isEmpty())
        return true;

    // Guard against nested transactions (see MessageRepository::saveMessages).
    bool ownsTransaction = false;
    if (!Transaction::isActive(db.connectionName())) {
        if (!db.transaction()) {
            qCWarning(LogDB) << "UserRepository: failed to start transaction";
            return false;
        }
        ownsTransaction = true;
    }

    for (const auto &user : users)
        if (!saveUser(user, db))
            goto rollback;

    if (ownsTransaction && !db.commit()) {
        qCWarning(LogDB) << "UserRepository: failed to commit transaction";
        db.rollback();
        return false;
    }
    return true;

rollback:
    if (ownsTransaction)
        db.rollback();
    return false;
}

std::optional<Discord::User> UserRepository::getUser(Core::Snowflake userId)
{
    auto db = getDb();
    QSqlQuery q(db);
    q.prepare(R"(
        SELECT id, username, global_name, avatar, bot
        FROM users WHERE id = :id
    )");
    q.bindValue(":id", static_cast<qint64>(userId));

    if (!q.exec() || !q.next())
        return std::nullopt;

    Discord::User user;
    user.id = static_cast<Core::Snowflake>(q.value(0).toLongLong());
    user.username = q.value(1).toString();
    if (!q.value(2).isNull())
        user.globalName = q.value(2).toString();
    if (!q.value(3).isNull())
        user.avatar = q.value(3).toString();
    if (!q.value(4).isNull())
        user.bot = q.value(4).toBool();

    return user;
}

} // namespace Storage
} // namespace Acheron

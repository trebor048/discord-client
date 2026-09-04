#include "UserRepository.hpp"

#include "DatabaseManager.hpp"
#include "Transaction.hpp"
#include "Core/Logging.hpp"

namespace Acheron {
namespace Storage {

namespace {
const char *const USER_UPSERT_SQL = R"(
    INSERT INTO users
    (id, username, global_name, avatar, bot)
    VALUES (:id, :username, :global_name, :avatar, :bot)
    ON CONFLICT(id) DO UPDATE SET
        username = excluded.username,
        global_name = excluded.global_name,
        avatar = excluded.avatar,
        bot = excluded.bot
)";
} // namespace

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
    q.prepare(QLatin1String(USER_UPSERT_SQL));
    bindUser(q, user);

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

    // Prepare the user upsert once and rebind per row instead of re-preparing
    // an INSERT per user (see MessageRepository::saveMessages for the same
    // pattern applied to embedded authors).
    QSqlQuery q(db);
    q.prepare(QLatin1String(USER_UPSERT_SQL));

    for (const auto &user : users) {
        bindUser(q, user);
        if (!execLogged(q, "UserRepository: Save user"))
            goto rollback;
    }

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

void UserRepository::bindUser(QSqlQuery &q, const Discord::User &user)
{
    q.bindValue(":id", static_cast<qint64>(user.id.get()));
    q.bindValue(":username", user.username);
    bindOptional(q, ":global_name", user.globalName);
    bindOptional(q, ":avatar", user.avatar);
    bindOptional(q, ":bot", user.bot);
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

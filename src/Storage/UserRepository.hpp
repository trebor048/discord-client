#pragma once

#include <QSqlDatabase>
#include <optional>

#include "BaseRepository.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

namespace Acheron {
namespace Storage {

class UserRepository : public BaseRepository
{
public:
    UserRepository(Core::Snowflake accountId);

    bool saveUser(const Discord::User &user);
    bool saveUser(const Discord::User &user, QSqlDatabase &db);
    bool saveUsers(const QList<Discord::User> &users);
    bool saveUsers(const QList<Discord::User> &users, QSqlDatabase &db);

    std::optional<Discord::User> getUser(Core::Snowflake userId);

private:
    static void bindUser(QSqlQuery &q, const Discord::User &user);
};

} // namespace Storage
} // namespace Acheron

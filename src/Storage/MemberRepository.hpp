#pragma once

#include <QSqlDatabase>
#include <optional>

#include "BaseRepository.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

namespace Acheron {
namespace Storage {

class MemberRepository : public BaseRepository
{
public:
    MemberRepository(Core::Snowflake accountId);

    bool saveMember(Core::Snowflake guildId, Core::Snowflake userId, const Discord::Member &member);
    bool saveMember(Core::Snowflake guildId, Core::Snowflake userId, const Discord::Member &member,
                    QSqlDatabase &db);
    bool saveMembers(Core::Snowflake guildId, const QList<Discord::Member> &members);
    void deleteMembersForGuild(Core::Snowflake guildId, QSqlDatabase &db);
    void deleteMember(Core::Snowflake guildId, Core::Snowflake userId);

    std::optional<Discord::Member> getMember(Core::Snowflake guildId, Core::Snowflake userId);
    QList<Core::Snowflake> getMemberUserIds(Core::Snowflake guildId);

private:
    static QString rolesToJson(const QList<Core::Snowflake> &roles);
    static QList<Core::Snowflake> rolesFromJson(const QString &json);
    static void bindMember(QSqlQuery &q, Core::Snowflake guildId, Core::Snowflake userId,
                           const Discord::Member &member);
};

} // namespace Storage
} // namespace Acheron

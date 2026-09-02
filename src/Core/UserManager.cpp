#include "UserManager.hpp"

#include "Logging.hpp"

namespace Acheron {
namespace Core {

UserManager::UserManager(Snowflake accountId, QObject *parent)
    : QObject(parent), userRepo(accountId), memberRepo(accountId)
{
    // QCache defaults to maxCost 100 with cost 1 per insert, so after ~100
    // entries everything evicts and getUser()/getMember() fall through to
    // synchronous SQLite on the UI thread. Large guilds hold thousands of
    // users; size the caches to keep them memory-resident.
    userCache.setMaxCost(10000);
    memberCache.setMaxCost(10000);
}

UserManager::~UserManager() {}

std::optional<Discord::User> UserManager::getUser(Snowflake userId)
{
    if (auto *user = userCache.object(userId))
        return *user;

    auto dbUser = userRepo.getUser(userId);
    if (!dbUser.has_value())
        return std::nullopt;

    userCache.insert(userId, new Discord::User(dbUser.value()));
    return dbUser;
}

QList<Snowflake> UserManager::cachedUserIds() const
{
    return userCache.keys();
}

std::optional<Discord::Member> UserManager::getMember(Snowflake guildId, Snowflake userId)
{
    MemberKey key{ guildId, userId };

    if (auto *member = memberCache.object(key))
        return *member;

    auto dbMember = memberRepo.getMember(guildId, userId);
    auto dbUser = userRepo.getUser(userId);
    if (!dbMember.has_value() || !dbUser.has_value())
        return std::nullopt;

    dbMember->user = dbUser.value();
    memberCache.insert(key, new Discord::Member(dbMember.value()));
    return dbMember;
}

std::optional<QList<Snowflake>> UserManager::getMemberRoles(Snowflake guildId, Snowflake userId)
{
    // Populate the member cache on miss so repeated role lookups for the same
    // member (role mentions, notification mention checks) do not re-query
    // SQLite on every call.
    auto member = getMember(guildId, userId);
    if (member.has_value())
        return member->roles.hasValue() ? member->roles.get() : QList<Snowflake>{};

    // getMember() additionally requires the users row, but roles live on the
    // member row alone; fall back to it directly so a member whose user row is
    // missing/uncached still returns its roles.
    auto dbMember = memberRepo.getMember(guildId, userId);
    if (!dbMember.has_value())
        return std::nullopt;
    return dbMember->roles.hasValue() ? dbMember->roles.get() : QList<Snowflake>{};
}

QString UserManager::getDisplayName(Snowflake userId, Snowflake guildId)
{
    auto user = getUser(userId);
    if (!user)
        return tr("Unknown User");

    if (guildId.isValid()) {
        auto member = getMember(guildId, userId);
        if (member && member->nick.hasValue())
            return member->nick;
    }

    return user->getDisplayName();
}

void UserManager::saveUser(const Discord::User &user)
{
    userCache.insert(user.id, new Discord::User(user));
    userRepo.saveUser(user);

    // Members embed their user at insert time; refresh the embedded copy so
    // consumers reading member->user (profile popups, member list fallbacks,
    // pinned-message authors) see the updated global name/avatar instead of
    // stale data until cache eviction.
    const QList<MemberKey> keys = memberCache.keys();
    for (const MemberKey &key : keys) {
        if (key.userId != user.id)
            continue;
        if (Discord::Member *m = memberCache.object(key)) {
            if (m->user.hasValue())
                m->user = user;
        }
    }
}

void UserManager::saveUsers(const QList<Discord::User> &users)
{
    for (const auto &user : users)
        userCache.insert(user.id, new Discord::User(user));

    userRepo.saveUsers(users);
}

void UserManager::saveMember(Snowflake guildId, Snowflake userId, const Discord::Member &member)
{
    MemberKey key{ guildId, userId };

    memberCache.insert(key, new Discord::Member(member));
    memberRepo.saveMember(guildId, userId, member);
}

void UserManager::saveMembers(Snowflake guildId, const QList<Discord::Member> &members)
{
    for (const auto &member : members) {
        if (!member.user.hasValue() || !member.user->id.hasValue())
            continue;
        Snowflake userId = member.user->id.get();
        memberCache.insert(MemberKey{ guildId, userId }, new Discord::Member(member));
    }
    memberRepo.saveMembers(guildId, members);
}

void UserManager::removeGuildMembers(Snowflake guildId)
{
    const QList<MemberKey> keys = memberCache.keys();
    for (const MemberKey &key : keys)
        if (key.guildId == guildId)
            memberCache.remove(key);
}

void UserManager::removeGuildMember(Snowflake guildId, Snowflake userId)
{
    memberCache.remove(MemberKey{ guildId, userId });
    memberRepo.deleteMember(guildId, userId);
}

void UserManager::saveMemberWithUser(Snowflake guildId, const Discord::Member &member)
{
    if (member.user.hasValue()) {
        saveUser(member.user.get());
        saveMember(guildId, member.user->id, member);
    }
}

void UserManager::loadNotesFromReady(const QHash<Snowflake, QString> &readyNotes)
{
    notes = readyNotes;
    qCInfo(LogCore) << "Loaded" << notes.size() << "user notes from READY";
}

void UserManager::setCachedNote(Snowflake userId, const QString &note)
{
    if (note.isEmpty())
        notes.remove(userId);
    else
        notes.insert(userId, note);
    emit noteChanged(userId);
}

std::optional<QString> UserManager::getCachedNote(Snowflake userId) const
{
    auto it = notes.constFind(userId);
    if (it == notes.constEnd())
        return std::nullopt;
    return it.value();
}

} // namespace Core
} // namespace Acheron

#include "TypingTracker.hpp"
#include "UserManager.hpp"

namespace Acheron {
namespace Core {

TypingTracker::TypingTracker(QObject *parent) : QObject(parent)
{
    cleanupTimer.setInterval(1000);
    connect(&cleanupTimer, &QTimer::timeout, this, &TypingTracker::cleanupExpired);
    cleanupTimer.start();
}

void TypingTracker::setUserManager(UserManager *manager)
{
    userManager = manager;
}

void TypingTracker::setActiveChannel(Snowflake channelId)
{
    if (activeChannel != channelId) {
        activeChannel = channelId;
        emit typersChanged();
    }
}

void TypingTracker::setCurrentUserId(Snowflake userId)
{
    currentUserId = userId;
}

void TypingTracker::addTyper(Snowflake channelId, Snowflake userId,
                             std::optional<Snowflake> guildId)
{
    if (userId == currentUserId)
        return;

    QDateTime expiresAt = QDateTime::currentDateTime().addSecs(10);
    TypingEntry entry{ userId, guildId, expiresAt };

    auto &typers = channelTypers[channelId];

    bool found = false;
    for (auto &t : typers) {
        if (t.userId == userId) {
            t.expiresAt = expiresAt;
            found = true;
            break;
        }
    }
    if (!found) {
        typers.append(entry);
    }

    if (channelId == activeChannel) {
        emit typersChanged();
    }
}

void TypingTracker::removeTyper(Snowflake channelId, Snowflake userId)
{
    auto &typers = channelTypers[channelId];
    auto it = std::find_if(typers.begin(), typers.end(),
                           [userId](const TypingEntry &t) { return t.userId == userId; });
    if (it != typers.end()) {
        typers.erase(it);
        emit typersChanged();
    }
}

void TypingTracker::clear()
{
    channelTypers.clear();
    emit typersChanged();
}

QList<TyperInfo> TypingTracker::getActiveTypers() const
{
    if (!userManager || !activeChannel.isValid())
        return {};

    auto it = channelTypers.find(activeChannel);
    if (it == channelTypers.end() || it.value().isEmpty())
        return {};

    QList<TyperInfo> typers;
    for (const auto &entry : it.value()) {
        TyperInfo info;
        info.userId = entry.userId;
        info.guildId = entry.guildId;
        info.name = userManager->getDisplayName(entry.userId, entry.guildId.value_or(Snowflake::Invalid));
        typers.append(info);
    }
    return typers;
}

void TypingTracker::cleanupExpired()
{
    QDateTime now = QDateTime::currentDateTime();
    bool activeChannelChanged = false;

    for (auto it = channelTypers.begin(); it != channelTypers.end();) {
        auto &list = it.value();
        int before = list.size();

        list.erase(std::remove_if(list.begin(), list.end(),
                                  [&now](const TypingEntry &e) { return e.expiresAt <= now; }),
                   list.end());

        if (list.size() != before && it.key() == activeChannel)
            activeChannelChanged = true;

        if (list.isEmpty()) {
            it = channelTypers.erase(it);
        } else {
            ++it;
        }
    }

    if (activeChannelChanged)
        emit typersChanged();
}

} // namespace Core
} // namespace Acheron

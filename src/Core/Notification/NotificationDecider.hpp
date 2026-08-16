#pragma once

#include "Core/Notification/NotificationTypes.hpp"
#include "Discord/Enums.hpp"

namespace Acheron {
namespace Core {
namespace Notification {

// Per-message facts the caller resolves from the client instance and hands to
// decide(). Keeping the decider free of ClientInstance makes it unit-testable.
struct DecideContext
{
    bool isBot = false;
    bool isOwnMessage = false;
    bool isMention = false;
    bool isFriend = false;
    bool authorOnNotifyList = false;
    bool authorOnIgnoreList = false;
    bool isActiveChannel = false;
    Discord::ChannelType channelType = Discord::ChannelType::GUILD_TEXT;
    // Resolved notification level (already collapses channel-mute -> NO_MESSAGES
    // and guild-mute -> ONLY_MENTIONS, as NotificationManager::getChannelNotificationLevel does).
    Discord::MessageNotificationLevel channelLevel =
            Discord::MessageNotificationLevel::ALL_MESSAGES;
};

struct Decision
{
    bool notify = false;
    bool redact = false;
    NotificationType type = NotificationType::Message;
};

// Pure notification decision. Mirrors NotificationManager::shouldShowNotification
// plus streamer-mode treatment. DND suppression and sound selection stay with
// the caller (they are type-dependent and already handled in showNotification).
Decision decide(const DecideContext &ctx, const NotificationSettings &settings, bool isStreaming);

} // namespace Notification
} // namespace Core
} // namespace Acheron

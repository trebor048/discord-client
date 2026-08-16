#include "NotificationDecider.hpp"

namespace Acheron {
namespace Core {
namespace Notification {

Decision decide(const DecideContext &ctx, const NotificationSettings &settings, bool isStreaming)
{
    Decision d;

    // Streamer mode: ignore entirely, or redact content.
    if (isStreaming) {
        if (settings.disableInStreamerMode)
            return d;
        if (settings.streamingTreatment == NotificationSettings::StreamingTreatment::Ignore)
            return d;
        if (settings.streamingTreatment == NotificationSettings::StreamingTreatment::NoContent)
            d.redact = true;
    }

    if (!settings.enabled)
        return d;

    if (ctx.isOwnMessage || ctx.isBot)
        return d;
    if (ctx.authorOnIgnoreList)
        return d;
    if (ctx.isActiveChannel)
        return d;

    // Resolve the notification type (used downstream for DND + sound).
    if (ctx.channelType == Discord::ChannelType::DM)
        d.type = NotificationType::DirectMessage;
    else if (ctx.channelType == Discord::ChannelType::GROUP_DM)
        d.type = NotificationType::GroupMessage;
    else if (ctx.isMention)
        d.type = NotificationType::Mention;
    else
        d.type = NotificationType::Message;

    // Allowlist always notifies, regardless of channel/server mute.
    if (ctx.authorOnNotifyList) {
        d.notify = true;
        return d;
    }

    if (ctx.channelType == Discord::ChannelType::DM) {
        d.notify = settings.notifyDirectMessages;
        return d;
    }
    if (ctx.channelType == Discord::ChannelType::GROUP_DM) {
        d.notify = settings.notifyGroupMessages;
        return d;
    }

    // Guild channel. Friend and mention notifications bypass the channel's
    // mute/notification level (matches NotificationManager::shouldShowNotification).
    if (settings.notifyFriendServerMessages && ctx.isFriend) {
        d.notify = true;
        return d;
    }
    if (settings.notifyMentions && ctx.isMention) {
        d.notify = true;
        return d;
    }
    if (settings.respectServerSettings) {
        if (ctx.channelLevel == Discord::MessageNotificationLevel::ALL_MESSAGES)
            d.notify = true;
        else if (ctx.channelLevel == Discord::MessageNotificationLevel::ONLY_MENTIONS)
            // Respect the master "notify on mention" toggle even in mentions-only
            // channels (the earlier isMention short-circuit above only fires when
            // notifyMentions is already on).
            d.notify = settings.notifyMentions && ctx.isMention;
        else // NO_MESSAGES (e.g. muted channel)
            d.notify = false;
        return d;
    }

    d.notify = false;
    return d;
}

} // namespace Notification
} // namespace Core
} // namespace Acheron

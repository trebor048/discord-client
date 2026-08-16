#pragma once

#include <QString>
#include <QColor>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QStringList>
#include <QVariant>
#include <functional>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
namespace Notification {

enum class NotificationType {
    Message,
    Mention,
    DirectMessage,
    GroupMessage,
    FriendRequest,
    FriendAccepted,
    VoiceJoin,
    VoiceLeave,
    VoiceMove,
    Custom
};

enum class NotificationPosition {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center
};

enum class NotificationSoundType {
    Default,
    Message1,
    Message2,
    Message3,
    Mention1,
    Mention2,
    Mention3,
    Custom
};

struct SoundOverride {
    bool enabled = false;
    QString selectedSound = "default";
    int volume = 100;
    QString customFileId;
    QString customUrl;
};

struct UserSoundMapping {
    bool enabled = false;
    QString selectedSound = "default";
    int volume = 100;
    QString customFileId;
    QString customUrl;
};

// A quick-action button rendered on a toast (e.g. "Reply"). The manager maps
// action ids to behavior; the widget only reports which action was triggered.
struct ToastAction {
    QString id;
    QString label;
};

inline bool operator==(const ToastAction &lhs, const ToastAction &rhs)
{
    return lhs.id == rhs.id && lhs.label == rhs.label;
}

inline bool operator!=(const ToastAction &lhs, const ToastAction &rhs)
{
    return !(lhs == rhs);
}

struct ToastNotificationData {
    QString title;
    QString body;
    QString authorName;
    QString iconUrl;
    QString thumbnailUrl;
    int attachments = 0;
    int timeout = 5;
    int opacity = 95;
    QString channelName;
    QString channelId;
    QString guildName;
    QString guildId;
    Core::Snowflake authorId;
    Core::Snowflake messageId = Core::Snowflake::Invalid;
    QColor badgeColor;
    QColor channelColor;
    bool coloredAccents = true;
    NotificationType type = NotificationType::Message;

    // Grouping: toasts sharing a non-empty groupKey collapse into one widget
    // whose messageCount is incremented for each collapsed notification.
    QString groupKey;
    int messageCount = 1;

    bool pauseOnHover = true;

    // Visual behavior knobs, copied from NotificationSettings at display time
    bool animationsEnabled = true;
    bool showProgressBar = true;

    QList<ToastAction> actions;

    std::function<void()> onClick;
    std::function<void()> onIconClick;
    std::function<void()> onDismiss;
    std::function<void(const QString &actionId)> onAction;
};

struct NotificationSettings {
    // Master switch: disables all notification output (toasts, native, sound)
    bool enabled = true;

    // Delivery backend: in-app custom toasts, OS-native tray messages, or both
    enum class DeliveryMode { InApp, Native, Both };
    DeliveryMode deliveryMode = DeliveryMode::InApp;

    // Where in-app toasts are anchored: inside the main window, on the monitor,
    // or auto (in-window while the app is focused, monitor while unfocused).
    enum class ToastPlacement { InWindow, Monitor, Auto };
    ToastPlacement toastPlacement = ToastPlacement::Monitor;

    // Grouping: collapse multiple messages from the same conversation into one toast
    bool groupingEnabled = true;

    // Appearance
    NotificationPosition position = NotificationPosition::BottomLeft;
    int maxNotifications = 3;
    int timeoutSeconds = 5;
    int opacity = 95;
    int edgeOffset = 20;
    double scaleFactor = 1.0;
    bool pauseOnHover = true;
    bool renderImages = true;
    bool animationsEnabled = true;
    bool progressBarEnabled = true;
    bool coloredAccents = true;

    // Notification Types
    bool notifyMentions = true;
    bool notifyDirectMessages = true;
    bool notifyGroupMessages = true;
    bool notifyFriendServerMessages = true;
    bool notifyFriendRequests = true;
    bool respectServerSettings = true;

    // Quiet hours: suppress toasts between start and end (HH:mm).
    bool quietHoursEnabled = false;
    QString quietHoursStart = QStringLiteral("22:00");
    QString quietHoursEnd = QStringLiteral("07:00");

    // Privacy & Streaming
    bool disableInStreamerMode = true;
    enum class StreamingTreatment { Normal, NoContent, Ignore };
    StreamingTreatment streamingTreatment = StreamingTreatment::Normal;

    // Voice
    bool notifyVoiceChannelJoins = false;
    int voiceDebounceMs = 2000;

    // Sound
    int globalSoundVolume = 100;
    bool soundForDMs = true;
    bool soundForGroupDMs = true;
    bool soundForMentions = true;
    bool soundForFriendServerMessages = true;
    bool soundForFriendRequests = true;
    QHash<QString, SoundOverride> soundOverrides;
    QHash<QString, UserSoundMapping> userSounds;

    // Native Notifications
    enum class NativeMode { Never, Always, NotFocused };
    NativeMode nativeMode = NativeMode::NotFocused;

    // Lists
    QStringList notifyForList;
    QStringList ignoreUsersList;
};

QString positionToString(NotificationPosition pos);
NotificationPosition stringToPosition(const QString &str);
QString streamingTreatmentToString(NotificationSettings::StreamingTreatment t);
NotificationSettings::StreamingTreatment stringToStreamingTreatment(const QString &str);
QString nativeModeToString(NotificationSettings::NativeMode m);
NotificationSettings::NativeMode stringToNativeMode(const QString &str);
QString deliveryModeToString(NotificationSettings::DeliveryMode m);
NotificationSettings::DeliveryMode stringToDeliveryMode(const QString &str);
QString toastPlacementToString(NotificationSettings::ToastPlacement p);
NotificationSettings::ToastPlacement stringToToastPlacement(const QString &str);

QColor generateBadgeColor(const QString &id);

enum class ColorPalette {
    Avatar,
    Channel
};

QColor colorForSnowflake(const QString &seed, ColorPalette palette);

} // namespace Notification
} // namespace Core
} // namespace Acheron

#pragma once

#include <QObject>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QMutex>
#include <QHash>
#include <QSet>
#include <QSystemTrayIcon>
#include <optional>

#include "NotificationTypes.hpp"
#include "SoundManager.hpp"

#include "Core/ClientInstance.hpp"
#include "Core/MessageManager.hpp"
#include "Core/RelationshipManager.hpp"
#include "Core/UserManager.hpp"
#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"
#include "Discord/Client.hpp"

#include "UI/Widgets/ToastContainer.hpp"
#include "UI/Widgets/ToastNotification.hpp"

namespace Acheron {
namespace Core {

class ImageManager;

class NotificationManager : public QObject
{
    Q_OBJECT
public:
    explicit NotificationManager(Core::ClientInstance *instance, QObject *parent = nullptr);
    ~NotificationManager() override;

    void initialize();
    void shutdown();

    // Settings
    const Notification::NotificationSettings &settings() const { return m_settings; }
    void setSettings(const Notification::NotificationSettings &settings);
    void loadSettings();
    void saveSettings();

    // Sound management
    SoundManager *soundManager() { return &m_soundManager; }

    // Image provider for toast avatars and attachment thumbnails
    void setImageManager(Core::ImageManager *imageManager) { m_imageManager = imageManager; }

    // Accessors for resolving notify-list IDs to names/icons in the settings UI.
    [[nodiscard]] Core::ClientInstance *instance() const { return m_instance; }
    [[nodiscard]] Core::ImageManager *imageManager() const { return m_imageManager; }

    // Parent widget for in-window toasts (the main window).
    void setInWindowParent(QWidget *parent) { m_inWindowParent = parent; }

    // Notification display
    void showNotification(const Notification::ToastNotificationData &data);
    void dismissAllNotifications();

    // Active channel suppression
    void setActiveChannel(Core::Snowflake channelId);
    void setStreamerModeEnabled(bool enabled);

    // List management
    QSet<QString> notifyForList() const;
    QSet<QString> ignoreUsersList() const;
    QSet<QString> ignoreEntitiesList() const;
    void addToNotifyList(const QString &id);
    void removeFromNotifyList(const QString &id);
    void addToIgnoreList(const QString &id);
    void removeFromIgnoreList(const QString &id);
    void addToIgnoreSet(const QString &id);
    void removeFromIgnoreSet(const QString &id);

    // Sound overrides
    Notification::SoundOverride getSoundOverride(const QString &soundId) const;
    void setSoundOverride(const QString &soundId, const Notification::SoundOverride &override);
    Notification::UserSoundMapping getUserSound(const QString &userId) const;
    void setUserSound(const QString &userId, const Notification::UserSoundMapping &sound);

    // Test (bypasses the master enable switch so the preview always renders)
    void sendTestNotification();
    bool requestNativeNotificationPermission(QString *errorMessage = nullptr);

signals:
    void settingsChanged();
    void soundOverrideChanged(const QString &soundId);
    void userSoundChanged(const QString &userId);
    void openUserProfileRequested(Core::Snowflake userId);
    void openChannelRequested(Core::Snowflake channelId);
    void jumpToMessageRequested(Core::Snowflake channelId, Core::Snowflake messageId);
    void openDmWithUserRequested(Core::Snowflake userId);
    void openFriendsTabRequested();

private slots:
    void onMessageCreated(const Discord::Message &message);
    void onVoiceStateUpdated(const Discord::VoiceState &state);
    void onRelationshipAdded(const Discord::Relationship &relationship);
    void onRelationshipRemoved(const Discord::RelationshipPartial &relationship);
    void onReady(const Discord::Ready &ready);

    void checkStreamerMode();

    void onReplySendSucceeded(const QString &nonce);
    void onReplySendFailed(const QString &nonce);

private:
    // Notification logic
    bool shouldShowNotification(const Discord::Message &message, const Discord::Channel &channel);
    bool shouldShowVoiceNotification(const Discord::VoiceState &newState, const Discord::VoiceState *oldState);
    bool isInQuietHours() const;
    Notification::ToastNotificationData createMessageNotification(const Discord::Message &message, const Discord::Channel &channel);
    Notification::ToastNotificationData createVoiceNotification(const Discord::User &user, const Discord::Channel &channel, const QString &action);
    Notification::ToastNotificationData createRelationshipNotification(const Discord::Relationship &relationship);

    // Helpers
    QString getUserDisplayName(const Discord::User &user) const;
    QString getUserDisplayName(const Discord::Member &member) const;
    bool isMentioned(const Discord::Message &message) const;
    Discord::MessageNotificationLevel getChannelNotificationLevel(const Discord::Channel &channel) const;
    std::optional<Discord::Guild> getGuild(Core::Snowflake guildId) const;
    std::optional<Discord::User> getUser(Core::Snowflake userId) const;

    // Streamer mode
    struct StreamerModeState {
        bool shouldIgnore = false;
        bool shouldRedact = false;
    };
    StreamerModeState evaluateStreamerMode();

    // Do Not Disturb suppression
    bool isOwnPresenceDnd() const;

    // Sound selection
    QString selectSoundForNotification(const Notification::ToastNotificationData &data);
    bool shouldPlaySoundForType(const Notification::ToastNotificationData &data) const;

    // Native notifications
    bool ensureNativeNotificationTray(QString *errorMessage = nullptr);
    void showNativeNotification(const Notification::ToastNotificationData &data);

    // In-app toast display, including grouping collapse
    void displayToast(const Notification::ToastNotificationData &data);
    // Re-applies the toast placement (in-window vs monitor) from settings.
    void applyToastPlacement();

    // Sends an inline-reply composed inside a toast and reports the outcome
    // back to that toast's busy/sent/failed indicator.
    void sendToastReply(UI::ToastNotification *toast,
                        const Notification::ToastNotificationData &data,
                        const QString &text);

    Core::ClientInstance *m_instance = nullptr;
    Core::ImageManager *m_imageManager = nullptr;
    QWidget *m_inWindowParent = nullptr;
    Notification::NotificationSettings m_settings;

    SoundManager m_soundManager;
    UI::ToastContainer *m_toastContainer = nullptr;
    QSystemTrayIcon *m_systemTray = nullptr;

    // Caches
    mutable QMutex m_notifyListMutex;
    QSet<QString> m_notifyForSet;
    QSet<QString> m_ignoreUsersSet;
    QSet<QString> m_ignoreSet; // channels and guilds to suppress toasts for
    bool m_listsLoaded = false;

    QHash<QString, Notification::SoundOverride> m_soundOverrides;
    QHash<QString, Notification::UserSoundMapping> m_userSounds;

    // Streamer mode tracking
    bool m_streamerModeEnabled = false;
    bool m_isStreaming = false;
    QTimer *m_streamerTimer = nullptr;

    // Voice debounce
    QHash<Core::Snowflake, qint64> m_lastVoiceNotification;
    QList<qint64> m_voiceTimestamps;
    qint64 m_lastVoiceUpdate = 0;

    // Voice state tracking (for detecting leave/move)
    struct VoiceStateInfo {
        std::optional<Core::Snowflake> channelId;
        qint64 timestamp = 0;
    };
    QHash<Core::Snowflake, VoiceStateInfo> m_voiceStates;

    // Active channel (don't notify for this channel)
    Core::Snowflake m_activeChannelId = Core::Snowflake::Invalid;

    // Toast reply outcomes are tracked at manager level so the connection from
    // MessageManager to the toast survives account switches (which recreate
    // MessageManager) and is cleaned up reliably when this manager is destroyed.
    QPointer<Core::MessageManager> m_replyMessages;
    QHash<QString, QPointer<UI::ToastNotification>> m_pendingReplyToasts;

    // Current user cache
    mutable QMutex m_currentUserMutex;
    std::optional<Discord::User> m_cachedCurrentUser;
    qint64 m_cachedCurrentUserTime = 0;
    static constexpr qint64 CURRENT_USER_CACHE_TTL = 1000;

    // Notification context for user-specific sounds
    struct NotificationContext {
        QString authorId;
        QString channelId;
        bool isDM = false;
        bool userOnNotifyList = false;
        bool channelOnNotifyList = false;
    };
    std::optional<NotificationContext> m_lastNotificationContext;
};

} // namespace Core
} // namespace Acheron

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

    // Notification display
    void showNotification(const Notification::ToastNotificationData &data);
    void dismissAllNotifications();

    // Active channel suppression
    void setActiveChannel(Core::Snowflake channelId);
    void setStreamerModeEnabled(bool enabled);

    // List management
    QSet<QString> notifyForList() const;
    QSet<QString> ignoreUsersList() const;
    void addToNotifyList(const QString &id);
    void removeFromNotifyList(const QString &id);
    void addToIgnoreList(const QString &id);
    void removeFromIgnoreList(const QString &id);

    // Sound overrides
    Notification::SoundOverride getSoundOverride(const QString &soundId) const;
    void setSoundOverride(const QString &soundId, const Notification::SoundOverride &override);
    Notification::UserSoundMapping getUserSound(const QString &userId) const;
    void setUserSound(const QString &userId, const Notification::UserSoundMapping &sound);

    // Test
    void sendTestNotification();
    bool requestNativeNotificationPermission(QString *errorMessage = nullptr);

signals:
    void settingsChanged();
    void soundOverrideChanged(const QString &soundId);
    void userSoundChanged(const QString &userId);
    void openUserProfileRequested(Core::Snowflake userId);
    void openChannelRequested(Core::Snowflake channelId);
    void openDmWithUserRequested(Core::Snowflake userId);
    void openFriendsTabRequested();

private slots:
    void onMessageCreated(const Discord::Message &message);
    void onVoiceStateUpdated(const Discord::VoiceState &state);
    void onRelationshipAdded(const Discord::Relationship &relationship);
    void onRelationshipRemoved(const Discord::RelationshipPartial &relationship);
    void onReady(const Discord::Ready &ready);

    void checkStreamerMode();

private:
    // Notification logic
    bool shouldShowNotification(const Discord::Message &message, const Discord::Channel &channel);
    bool shouldShowVoiceNotification(const Discord::VoiceState &newState, const Discord::VoiceState *oldState);
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

    Core::ClientInstance *m_instance = nullptr;
    Notification::NotificationSettings m_settings;

    SoundManager m_soundManager;
    UI::ToastContainer *m_toastContainer = nullptr;
    QSystemTrayIcon *m_systemTray = nullptr;

    // Caches
    mutable QMutex m_notifyListMutex;
    QSet<QString> m_notifyForSet;
    QSet<QString> m_ignoreUsersSet;
    bool m_listsLoaded = false;

    QHash<QString, Notification::SoundOverride> m_soundOverrides;
    QHash<QString, Notification::UserSoundMapping> m_userSounds;

    // Streamer mode tracking
    bool m_streamerModeEnabled = false;
    bool m_isStreaming = false;

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

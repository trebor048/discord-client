#include "NotificationManager.hpp"

#include <QApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QCryptographicHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSystemTrayIcon>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <psapi.h>
#  pragma comment(lib, "psapi.lib")
#endif

#include "Core/ImageManager.hpp"
#include "Discord/CdnUrls.hpp"

#include "Core/ReadStateManager.hpp"

namespace {

bool streamingSoftwareRunning()
{
#ifdef Q_OS_WIN
    DWORD processes[1024];
    DWORD needed = 0;
    if (!EnumProcesses(processes, sizeof(processes), &needed))
        return false;

    DWORD count = needed / sizeof(DWORD);
    for (DWORD i = 0; i < count; ++i) {
        if (processes[i] == 0)
            continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                      processes[i]);
        if (!hProcess)
            continue;

        wchar_t name[MAX_PATH] = {};
        bool found = false;
        if (GetModuleBaseNameW(hProcess, nullptr, name, MAX_PATH) > 0) {
            const QString processName = QString::fromWCharArray(name).toLower();
            found = processName.contains(QStringLiteral("obs64")) ||
                    processName.contains(QStringLiteral("obs32")) ||
                    processName.contains(QStringLiteral("xsplit"));
        }
        CloseHandle(hProcess);
        if (found)
            return true;
    }
    return false;
#else
    QProcess process;
    process.start(QStringLiteral("pgrep"),
                  QStringList{QStringLiteral("-x"), QStringLiteral("obs")});
    if (!process.waitForFinished(1500))
        return false;
    return process.exitCode() == 0;
#endif
}

} // namespace

namespace Acheron {
namespace Core {

NotificationManager::NotificationManager(Core::ClientInstance *instance, QObject *parent)
    : QObject(parent), m_instance(instance)
{
}

NotificationManager::~NotificationManager()
{
    shutdown();
}

void NotificationManager::initialize()
{
    loadSettings();

    m_soundManager.initialize();

    // Create toast container
    m_toastContainer = new UI::ToastContainer(m_settings.position, nullptr);
    m_toastContainer->setMaxNotifications(m_settings.maxNotifications);
    m_toastContainer->setOpacity(m_settings.opacity);
    m_toastContainer->setScale(m_settings.scaleFactor);
    m_toastContainer->setEdgeOffset(m_settings.edgeOffset);

    connect(m_toastContainer, &UI::ToastContainer::notificationClicked,
            this, [this](const Notification::ToastNotificationData &data) {
        if (data.onClick) data.onClick();
    });
    connect(m_toastContainer, &UI::ToastContainer::notificationIconClicked,
            this, [this](const Notification::ToastNotificationData &data) {
        if (data.onIconClick) data.onIconClick();
    });

    // Connect to client instance signals
    if (m_instance) {
        connect(m_instance->discord(), &Discord::Client::messageCreated,
                this, &NotificationManager::onMessageCreated);
        connect(m_instance->discord(), &Discord::Client::voiceStateUpdated,
                this, &NotificationManager::onVoiceStateUpdated);
        connect(m_instance->discord(), &Discord::Client::relationshipAdded,
                this, &NotificationManager::onRelationshipAdded);
        connect(m_instance->discord(), &Discord::Client::relationshipRemoved,
                this, &NotificationManager::onRelationshipRemoved);
        connect(m_instance->discord(), &Discord::Client::ready,
                this, &NotificationManager::onReady);
    }

    // Streamer mode check timer
    auto *streamerTimer = new QTimer(this);
    streamerTimer->setInterval(5000);
    connect(streamerTimer, &QTimer::timeout, this, &NotificationManager::checkStreamerMode);
    streamerTimer->start();
}

void NotificationManager::shutdown()
{
    if (m_toastContainer) {
        m_toastContainer->dismissAll();
        m_toastContainer->deleteLater();
        m_toastContainer = nullptr;
    }
    if (m_systemTray) {
        m_systemTray->hide();
        m_systemTray->deleteLater();
        m_systemTray = nullptr;
    }
    m_soundManager.shutdown();
    saveSettings();
}

void NotificationManager::loadSettings()
{
    QSettings settings;

    // Appearance
    m_settings.position = Notification::stringToPosition(settings.value("notifications/position", "bottom-left").toString());
    m_settings.maxNotifications = settings.value("notifications/max_notifications", 3).toInt();
    m_settings.timeoutSeconds = settings.value("notifications/timeout", 5).toInt();
    m_settings.opacity = settings.value("notifications/opacity", 95).toInt();
    m_settings.edgeOffset = settings.value("notifications/edge_offset", 20).toInt();
    m_settings.scaleFactor = settings.value("notifications/scale", 1.0).toDouble();
    m_settings.pauseOnHover = settings.value("notifications/pause_on_hover", true).toBool();
    m_settings.renderImages = settings.value("notifications/render_images", true).toBool();

    // Notification types
    m_settings.notifyMentions = settings.value("notifications/mentions", true).toBool();
    m_settings.notifyDirectMessages = settings.value("notifications/direct_messages", true).toBool();
    m_settings.notifyGroupMessages = settings.value("notifications/group_messages", true).toBool();
    m_settings.notifyFriendServerMessages = settings.value("notifications/friend_server_messages", true).toBool();
    m_settings.notifyFriendRequests = settings.value("notifications/friend_requests", true).toBool();
    m_settings.respectServerSettings = settings.value("notifications/respect_server_settings", true).toBool();

    // Privacy
    m_settings.disableInStreamerMode = settings.value("notifications/disable_streamer_mode", true).toBool();
    m_settings.streamingTreatment = Notification::stringToStreamingTreatment(settings.value("notifications/streaming_treatment", "normal").toString());

    // Voice
    m_settings.notifyVoiceChannelJoins = settings.value("notifications/voice_joins", false).toBool();
    m_settings.voiceDebounceMs = settings.value("notifications/voice_debounce", 2000).toInt();

    // Sound
    m_settings.globalSoundVolume = settings.value("notifications/sound_volume", 100).toInt();
    m_soundManager.setGlobalVolume(m_settings.globalSoundVolume);
    m_settings.soundForDMs = settings.value("notifications/sound_for_dms", true).toBool();
    m_settings.soundForGroupDMs = settings.value("notifications/sound_for_group_dms", true).toBool();
    m_settings.soundForMentions = settings.value("notifications/sound_for_mentions", true).toBool();
    m_settings.soundForFriendServerMessages = settings.value("notifications/sound_for_friend_server_messages", true).toBool();
    m_settings.soundForFriendRequests = settings.value("notifications/sound_for_friend_requests", true).toBool();

    // Sound overrides
    QVariant overridesVar = settings.value("notifications/sound_overrides");
    if (overridesVar.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(overridesVar.toByteArray());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                QJsonObject overrideObj = it.value().toObject();
                Notification::SoundOverride override;
                override.enabled = overrideObj["enabled"].toBool(false);
                override.selectedSound = overrideObj["selected_sound"].toString("default");
                override.volume = overrideObj["volume"].toInt(100);
                override.customFileId = overrideObj["custom_file_id"].toString();
                if (override.customFileId.isEmpty())
                    override.customFileId = overrideObj["custom_file_path"].toString();
                override.customUrl = overrideObj["custom_url"].toString();
                m_soundOverrides[it.key()] = override;
            }
        }
    }

    // User sounds
    QVariant userSoundsVar = settings.value("notifications/user_sounds");
    if (userSoundsVar.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSoundsVar.toByteArray());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                QJsonObject soundObj = it.value().toObject();
                Notification::UserSoundMapping mapping;
                mapping.enabled = soundObj["enabled"].toBool(false);
                mapping.selectedSound = soundObj["selected_sound"].toString("default");
                mapping.volume = soundObj["volume"].toInt(100);
                mapping.customFileId = soundObj["custom_file_id"].toString();
                if (mapping.customFileId.isEmpty())
                    mapping.customFileId = soundObj["custom_file_path"].toString();
                mapping.customUrl = soundObj["custom_url"].toString();
                m_userSounds[it.key()] = mapping;
            }
        }
    }

    // Native notifications
    m_settings.nativeMode = Notification::stringToNativeMode(settings.value("notifications/native_mode", "not-focused").toString());

    // Lists
    QString notifyForStr = settings.value("notifications/notify_for", "").toString();
    if (!notifyForStr.isEmpty()) {
        const auto values = notifyForStr.split(",", Qt::SkipEmptyParts);
        m_notifyForSet = QSet<QString>(values.cbegin(), values.cend());
    }

    QString ignoreUsersStr = settings.value("notifications/ignore_users", "").toString();
    if (!ignoreUsersStr.isEmpty()) {
        const auto values = ignoreUsersStr.split(",", Qt::SkipEmptyParts);
        m_ignoreUsersSet = QSet<QString>(values.cbegin(), values.cend());
    }
    m_listsLoaded = true;
}

void NotificationManager::saveSettings()
{
    QSettings settings;

    settings.setValue("notifications/position", Notification::positionToString(m_settings.position));
    settings.setValue("notifications/max_notifications", m_settings.maxNotifications);
    settings.setValue("notifications/timeout", m_settings.timeoutSeconds);
    settings.setValue("notifications/opacity", m_settings.opacity);
    settings.setValue("notifications/edge_offset", m_settings.edgeOffset);
    settings.setValue("notifications/scale", m_settings.scaleFactor);
    settings.setValue("notifications/pause_on_hover", m_settings.pauseOnHover);
    settings.setValue("notifications/render_images", m_settings.renderImages);

    settings.setValue("notifications/mentions", m_settings.notifyMentions);
    settings.setValue("notifications/direct_messages", m_settings.notifyDirectMessages);
    settings.setValue("notifications/group_messages", m_settings.notifyGroupMessages);
    settings.setValue("notifications/friend_server_messages", m_settings.notifyFriendServerMessages);
    settings.setValue("notifications/friend_requests", m_settings.notifyFriendRequests);
    settings.setValue("notifications/respect_server_settings", m_settings.respectServerSettings);

    settings.setValue("notifications/disable_streamer_mode", m_settings.disableInStreamerMode);
    settings.setValue("notifications/streaming_treatment", Notification::streamingTreatmentToString(m_settings.streamingTreatment));

    settings.setValue("notifications/voice_joins", m_settings.notifyVoiceChannelJoins);
    settings.setValue("notifications/voice_debounce", m_settings.voiceDebounceMs);

    settings.setValue("notifications/sound_volume", m_settings.globalSoundVolume);
    settings.setValue("notifications/sound_for_dms", m_settings.soundForDMs);
    settings.setValue("notifications/sound_for_group_dms", m_settings.soundForGroupDMs);
    settings.setValue("notifications/sound_for_mentions", m_settings.soundForMentions);
    settings.setValue("notifications/sound_for_friend_server_messages", m_settings.soundForFriendServerMessages);
    settings.setValue("notifications/sound_for_friend_requests", m_settings.soundForFriendRequests);

    // Sound overrides
    QJsonObject overridesObj;
    for (auto it = m_soundOverrides.begin(); it != m_soundOverrides.end(); ++it) {
        QJsonObject overrideObj;
        overrideObj["enabled"] = it.value().enabled;
        overrideObj["selected_sound"] = it.value().selectedSound;
        overrideObj["volume"] = it.value().volume;
        overrideObj["custom_file_id"] = it.value().customFileId;
        overrideObj["custom_url"] = it.value().customUrl;
        overridesObj[it.key()] = overrideObj;
    }
    settings.setValue("notifications/sound_overrides", QJsonDocument(overridesObj).toJson(QJsonDocument::Compact));

    // User sounds
    QJsonObject userSoundsObj;
    for (auto it = m_userSounds.begin(); it != m_userSounds.end(); ++it) {
        QJsonObject soundObj;
        soundObj["enabled"] = it.value().enabled;
        soundObj["selected_sound"] = it.value().selectedSound;
        soundObj["volume"] = it.value().volume;
        soundObj["custom_file_id"] = it.value().customFileId;
        soundObj["custom_url"] = it.value().customUrl;
        userSoundsObj[it.key()] = soundObj;
    }
    settings.setValue("notifications/user_sounds", QJsonDocument(userSoundsObj).toJson(QJsonDocument::Compact));

    settings.setValue("notifications/native_mode", Notification::nativeModeToString(m_settings.nativeMode));

    settings.setValue("notifications/notify_for", m_notifyForSet.values().join(","));
    settings.setValue("notifications/ignore_users", m_ignoreUsersSet.values().join(","));

    settings.sync();
}

void NotificationManager::setSettings(const Notification::NotificationSettings &settings)
{
    m_settings = settings;

    if (m_toastContainer) {
        m_toastContainer->setMaxNotifications(m_settings.maxNotifications);
        m_toastContainer->setOpacity(m_settings.opacity);
        m_toastContainer->setScale(m_settings.scaleFactor);
        m_toastContainer->setEdgeOffset(m_settings.edgeOffset);
        m_toastContainer->setPosition(m_settings.position);
    }

    m_soundManager.setGlobalVolume(m_settings.globalSoundVolume);

    saveSettings();
    emit settingsChanged();
}

void NotificationManager::showNotification(const Notification::ToastNotificationData &data)
{
    // Do Not Disturb: suppress notification noise (toast, sound, and native
    // popups) while the user's own presence status is "dnd". Mentions and DMs
    // are still processed elsewhere; only the audible/visual noise is muted.
    if (isOwnPresenceDnd()) {
        switch (data.type) {
        case Notification::NotificationType::Message:
        case Notification::NotificationType::VoiceJoin:
        case Notification::NotificationType::VoiceLeave:
        case Notification::NotificationType::VoiceMove:
            return;
        default:
            break;
        }
    }

    if (!m_toastContainer) return;

    auto *toast = new UI::ToastNotification(data);
    toast->setOpacity(m_settings.opacity / 100.0);
    toast->setScale(m_settings.scaleFactor);
    m_toastContainer->addNotification(toast);

    // Play sound (only if notification sounds are enabled)
    const bool muteDuringStreamerMode =
            QSettings().value("streamer/mute_sounds", true).toBool()
            && (m_streamerModeEnabled || m_isStreaming);
    const bool dndMute = isOwnPresenceDnd();
    if (!dndMute && !muteDuringStreamerMode &&
        QSettings().value("notifications/sounds", true).toBool() &&
        shouldPlaySoundForType(data)) {
        QString soundId = selectSoundForNotification(data);
        if (!soundId.isEmpty()) {
            int volume = m_settings.globalSoundVolume;
            QString customFileId;
            QString customUrl;

            // Keep the precedence from selectSoundForNotification while carrying
            // the selected override's volume/custom file metadata to playback.
            bool userSoundSelected = false;
            if (data.authorId.isValid()) {
                auto userSound = getUserSound(data.authorId.toString());
                if (userSound.enabled) {
                    bool shouldUse = false;
                    if (m_lastNotificationContext.has_value()) {
                        const auto &ctx = m_lastNotificationContext.value();
                        shouldUse = ctx.userOnNotifyList || ctx.isDM || ctx.channelOnNotifyList;
                    }
                    if (shouldUse && soundId == userSound.selectedSound) {
                        volume = (volume * userSound.volume) / 100;
                        customFileId = userSound.customFileId;
                        customUrl = userSound.customUrl;
                        userSoundSelected = true;
                    }
                }
            }

            if (!userSoundSelected) {
                auto override = getSoundOverride(soundId);
                if (override.enabled) {
                    volume = (volume * override.volume) / 100;
                    customFileId = override.customFileId;
                    customUrl = override.customUrl;
                    if (!override.selectedSound.isEmpty() && override.selectedSound != QLatin1String("default") &&
                        override.selectedSound != soundId) {
                        soundId = override.selectedSound;
                    }
                }
            }

            if (soundId == QStringLiteral("default"))
                soundId = SoundManager::DefaultNotification;

            if (soundId == QStringLiteral("custom")) {
                if (!customUrl.isEmpty()) {
                    m_soundManager.playUrl(QUrl(customUrl), volume);
                } else if (!customFileId.isEmpty() && m_soundManager.hasCachedSound(customFileId)) {
                    m_soundManager.playNotificationSound(customFileId, volume);
                } else if (!customFileId.isEmpty()) {
                    QFile f(customFileId);
                    if (f.open(QIODevice::ReadOnly)) {
                        QByteArray audioData = f.readAll();
                        f.close();
                        QString ext = QFileInfo(customFileId).suffix().toLower();
                        m_soundManager.cacheSound(customFileId, audioData, ext);
                        m_soundManager.playCustomSound(audioData, ext, volume);
                    } else {
                        m_soundManager.playNotificationSound(SoundManager::DefaultNotification, volume);
                    }
                } else {
                    m_soundManager.playNotificationSound(SoundManager::DefaultNotification, volume);
                }
            } else {
                m_soundManager.playNotificationSound(soundId, volume);
            }
        }
    }

    // Native notifications
    if (m_settings.nativeMode != Notification::NotificationSettings::NativeMode::Never) {
        bool shouldShowNative = false;
        if (m_settings.nativeMode == Notification::NotificationSettings::NativeMode::Always) {
            shouldShowNative = true;
        } else if (m_settings.nativeMode == Notification::NotificationSettings::NativeMode::NotFocused) {
            shouldShowNative = !QApplication::activeWindow();
        }

        if (shouldShowNative) {
            showNativeNotification(data);
        }
    }
}

void NotificationManager::dismissAllNotifications()
{
    if (m_toastContainer) {
        m_toastContainer->dismissAll();
    }
}

void NotificationManager::setActiveChannel(Core::Snowflake channelId)
{
    m_activeChannelId = channelId;
}

void NotificationManager::onMessageCreated(const Discord::Message &message)
{
    if (!m_instance || !m_instance->discord()) return;

    const auto &author = message.author.get();
    const auto authorId = author.id.get();
    const auto channelId = message.channelId.get();

    // Ignore own messages
    if (authorId == m_instance->accountId()) return;

    // Ignore bots
    if (author.bot.getOr(false)) return;

    // Get channel
    auto channel = m_instance->getChannel(channelId);
    if (!channel) return;

    // Don't notify for current active channel
    if (channelId == m_activeChannelId) return;

    // Check streamer mode
    auto streamer = evaluateStreamerMode();
    if (streamer.shouldIgnore) return;

    // Check if we should show notification
    if (!shouldShowNotification(message, *channel)) return;

    // Store notification context for user-specific sounds
    m_lastNotificationContext = NotificationContext{
        .authorId = authorId.toString(),
        .channelId = channelId.toString(),
        .isDM = channel->type.get() == Discord::ChannelType::DM,
        .userOnNotifyList = m_notifyForSet.contains(authorId.toString()),
        .channelOnNotifyList = m_notifyForSet.contains(channelId.toString())
    };

    // Create and show notification
    auto notification = createMessageNotification(message, *channel);

    if (streamer.shouldRedact) {
        notification.body = "Message content hidden (Streaming)";
        notification.iconUrl.clear();
        notification.attachments = 0;
    }

    if ((m_streamerModeEnabled || m_isStreaming) &&
        QSettings().value("streamer/hide_invites", true).toBool()) {
        static const QRegularExpression invitePattern(
                QStringLiteral(R"((https?://)?(www\.)?(discord\.gg|discord(?:app)?\.com/invite)/[^\s]+)"),
                QRegularExpression::CaseInsensitiveOption);
        notification.body.replace(invitePattern, QStringLiteral("[invite hidden]"));
    }

    showNotification(notification);
}

void NotificationManager::onVoiceStateUpdated(const Discord::VoiceState &state)
{
    if (!m_settings.notifyVoiceChannelJoins) return;

    const auto userId = state.userId.get();

    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Global debounce
    if (now - m_lastVoiceUpdate < 500) return;
    m_lastVoiceUpdate = now;

    // Prune old timestamps (10 second window)
    qint64 windowStart = now - 10000;
    while (!m_voiceTimestamps.isEmpty() && m_voiceTimestamps.first() < windowStart) {
        m_voiceTimestamps.removeFirst();
    }
    if (m_voiceTimestamps.size() >= 20) return;
    m_voiceTimestamps.append(now);

    // Ignore self
    if (userId == m_instance->accountId()) return;

    // Check ignore list
    if (m_ignoreUsersSet.contains(userId.toString())) return;

    // Per-user debounce
    qint64 lastUserVoice = m_lastVoiceNotification.value(userId, 0);
    if (now - lastUserVoice < m_settings.voiceDebounceMs) return;
    m_lastVoiceNotification[userId] = now;

    auto user = getUser(userId);
    if (!user) return;

    bool isWatched = m_notifyForSet.contains(userId.toString());

    // Get previous voice state
    std::optional<Core::Snowflake> oldChannelId;
    if (m_voiceStates.contains(userId)) {
        oldChannelId = m_voiceStates[userId].channelId;
    }

    // Update voice state
    VoiceStateInfo newStateInfo;
    if (state.channelId.hasValue()) {
        newStateInfo.channelId = state.channelId.get();
    }
    newStateInfo.timestamp = now;
    m_voiceStates[userId] = newStateInfo;

    // Get channels
    std::optional<Discord::Channel> oldChannel;
    std::optional<Discord::Channel> newChannel;

    if (oldChannelId.has_value()) {
        oldChannel = m_instance->getChannel(oldChannelId.value());
    }
    if (state.channelId.hasValue()) {
        newChannel = m_instance->getChannel(state.channelId.get());
    }

    // Determine event type
    QString action;
    std::optional<Discord::Channel> targetChannel;

    if (!oldChannelId.has_value() && state.channelId.hasValue()) {
        // Joined a voice channel
        action = "joined";
        targetChannel = newChannel;
    } else if (oldChannelId.has_value() && !state.channelId.hasValue()) {
        // Left a voice channel
        action = "left";
        targetChannel = oldChannel;
    } else if (oldChannelId.has_value() && state.channelId.hasValue() && oldChannelId.value() != state.channelId.get()) {
        // Moved between voice channels
        action = "moved";
        // For moves, we'll send two notifications: left old, joined new
    }

    auto shouldNotify = [&](const std::optional<Discord::Channel>& channel) -> bool {
        if (!channel) return false;
        if (!shouldShowVoiceNotification(state, nullptr)) return false;
        // Mute check keyed on the target channel (for leaves, newState.channelId
        // is null so shouldShowVoiceNotification skips it).
        if (m_instance) {
            if (auto *rs = m_instance->readState()) {
                if (rs->isChannelMuted(channel->id.get()))
                    return false;
                if (channel->guildId.hasValue() && rs->isGuildMuted(channel->guildId.get()))
                    return false;
            }
        }
        if (isWatched) return true;
        if (channel->type.get() == Discord::ChannelType::GUILD_VOICE && m_notifyForSet.contains(channel->id.get().toString())) {
            return true;
        }
        return false;
    };

    // Handle join
    if (!oldChannelId.has_value() && state.channelId.hasValue()) {
        if (shouldNotify(newChannel)) {
            auto notification = createVoiceNotification(*user, *newChannel, "joined");
            m_lastNotificationContext = NotificationContext{
                .authorId = userId.toString(),
                .channelId = state.channelId.get().toString(),
                .isDM = false,
                .userOnNotifyList = isWatched,
                .channelOnNotifyList = m_notifyForSet.contains(newChannel->id.get().toString())
            };
            showNotification(notification);
        }
    }
    // Handle leave
    else if (oldChannelId.has_value() && !state.channelId.hasValue()) {
        if (shouldNotify(oldChannel)) {
            auto notification = createVoiceNotification(*user, *oldChannel, "left");
            m_lastNotificationContext = NotificationContext{
                .authorId = userId.toString(),
                .channelId = oldChannelId.value().toString(),
                .isDM = false,
                .userOnNotifyList = isWatched,
                .channelOnNotifyList = m_notifyForSet.contains(oldChannel->id.get().toString())
            };
            showNotification(notification);
        }
    }
    // Handle move (send both leave and join notifications)
    else if (oldChannelId.has_value() && state.channelId.hasValue() && oldChannelId.value() != state.channelId.get()) {
        // Left old channel
        if (shouldNotify(oldChannel)) {
            auto notification = createVoiceNotification(*user, *oldChannel, "left");
            m_lastNotificationContext = NotificationContext{
                .authorId = userId.toString(),
                .channelId = oldChannelId.value().toString(),
                .isDM = false,
                .userOnNotifyList = isWatched,
                .channelOnNotifyList = m_notifyForSet.contains(oldChannel->id.get().toString())
            };
            showNotification(notification);
        }
        // Joined new channel
        if (shouldNotify(newChannel)) {
            auto notification = createVoiceNotification(*user, *newChannel, "joined");
            m_lastNotificationContext = NotificationContext{
                .authorId = userId.toString(),
                .channelId = state.channelId.get().toString(),
                .isDM = false,
                .userOnNotifyList = isWatched,
                .channelOnNotifyList = m_notifyForSet.contains(newChannel->id.get().toString())
            };
            showNotification(notification);
        }
    }
}

void NotificationManager::onRelationshipAdded(const Discord::Relationship &relationship)
{
    if (!m_settings.notifyFriendRequests) return;

    if (m_ignoreUsersSet.contains(relationship.id.get().toString())) return;

    auto streamer = evaluateStreamerMode();
    if (streamer.shouldIgnore) return;

    auto notification = createRelationshipNotification(relationship);
    if ((m_streamerModeEnabled || m_isStreaming) &&
        QSettings().value("streamer/hide_personal_info", true).toBool() &&
        relationship.type.get() != Discord::RelationshipType::FRIEND) {
        notification.title = QStringLiteral("Friend request activity");
        notification.body = QStringLiteral("Open friends tab to review it.");
        notification.iconUrl.clear();
    }
    showNotification(notification);
}

void NotificationManager::onRelationshipRemoved(const Discord::RelationshipPartial &relationship)
{
    // Could notify on friend removal if needed
}

void NotificationManager::onReady(const Discord::Ready &ready)
{
    // Cache current user
    QMutexLocker locker(&m_currentUserMutex);
    m_cachedCurrentUser = ready.user;
    m_cachedCurrentUserTime = QDateTime::currentMSecsSinceEpoch();

    // Load notify/ignore lists from ready data if needed
}

bool NotificationManager::shouldShowNotification(const Discord::Message &message, const Discord::Channel &channel)
{
    if (!m_instance) return false;

    const auto &author = message.author.get();
    const auto authorId = author.id.get();

    if (!authorId.isValid()) return false;
    if (author.bot.getOr(false)) return false;

    // Check ignore list
    if (m_ignoreUsersSet.contains(authorId.toString())) return false;

    // Check notify list (always notify)
    if (m_notifyForSet.contains(authorId.toString())) return true;

    // Check channel type
    if (channel.type.get() == Discord::ChannelType::DM) {
        return m_settings.notifyDirectMessages;
    }
    if (channel.type.get() == Discord::ChannelType::GROUP_DM) {
        return m_settings.notifyGroupMessages;
    }

    // Guild channel
    if (channel.guildId.hasValue()) {
        // Friend in server
        if (m_settings.notifyFriendServerMessages) {
            if (m_instance->relationships()->isFriend(authorId)) {
                return true;
            }
        }

        // Mentions
        if (m_settings.notifyMentions && isMentioned(message)) {
            return true;
        }

        // Server notification settings
        if (m_settings.respectServerSettings) {
            auto level = getChannelNotificationLevel(channel);
            if (level == Discord::MessageNotificationLevel::ALL_MESSAGES) return true;
            if (level == Discord::MessageNotificationLevel::ONLY_MENTIONS) {
                return isMentioned(message);
            }
            return false;
        }

        return false;
    }

    return false;
}

bool NotificationManager::shouldShowVoiceNotification(const Discord::VoiceState &newState, const Discord::VoiceState *oldState)
{
    Q_UNUSED(oldState);

    // Check streamer mode
    auto streamer = evaluateStreamerMode();
    if (streamer.shouldIgnore)
        return false;

    // Check mute status via ReadStateManager
    if (newState.channelId.hasValue() && m_instance) {
        if (auto *rs = m_instance->readState()) {
            if (rs->isChannelMuted(newState.channelId.get()))
                return false;
            if (newState.guildId.hasValue() && rs->isGuildMuted(newState.guildId.get()))
                return false;
        }
    }

    // Do Not Disturb: suppress voice notification noise while the user's own
    // presence status is "dnd".
    if (isOwnPresenceDnd())
        return false;

    return true;
}

Notification::ToastNotificationData NotificationManager::createMessageNotification(const Discord::Message &message, const Discord::Channel &channel)
{
    const auto &author = message.author.get();
    const auto authorId = author.id.get();
    const auto channelId = message.channelId.get();

    Notification::ToastNotificationData data;
    data.title = QString("%1 (%2)").arg(getUserDisplayName(author)).arg(channel.name.getOr("#unknown"));
    data.body = message.content.get();
    data.channelName = channel.name.getOr("Unknown");
    data.channelId = channelId.toString();
    data.authorId = authorId;
    data.badgeColor = Notification::generateBadgeColor(authorId.toString());

    if (channel.type.get() == Discord::ChannelType::DM) {
        data.type = Notification::NotificationType::DirectMessage;
    } else if (channel.type.get() == Discord::ChannelType::GROUP_DM) {
        data.type = Notification::NotificationType::GroupMessage;
    } else if (isMentioned(message)) {
        data.type = Notification::NotificationType::Mention;
    } else {
        data.type = Notification::NotificationType::Message;
    }

    if (m_settings.renderImages && author.avatar.hasValue()) {
        data.iconUrl = Discord::Cdn::userAvatar(authorId, author.avatar.get(), 64).toString();
    }

    if (channel.guildId.hasValue()) {
        auto guild = getGuild(channel.guildId.get());
        if (guild) {
            data.guildName = guild->name;
            data.guildId = guild->id.get().toString();
        }
    }

    data.attachments = message.attachments.hasValue() ? message.attachments->size() : 0;

    data.onClick = [this, channelId]() {
        if (m_instance) {
            emit openChannelRequested(channelId);
        }
    };

    data.onIconClick = [this, userId = authorId]() {
        if (m_instance) {
            emit openUserProfileRequested(userId);
        }
    };

    return data;
}

Notification::ToastNotificationData NotificationManager::createVoiceNotification(const Discord::User &user, const Discord::Channel &channel, const QString &action)
{
    const auto userId = user.id.get();
    const auto channelId = channel.id.get();

    Notification::ToastNotificationData data;
    data.title = QString("%1 %2 voice").arg(getUserDisplayName(user)).arg(action);
    data.body = channel.name.getOr("Unknown Channel");
    data.channelName = channel.name.getOr("Unknown");
    data.channelId = channelId.toString();
    data.authorId = userId;
    data.badgeColor = Notification::generateBadgeColor(userId.toString());
    data.type = action == "joined" ? Notification::NotificationType::VoiceJoin : Notification::NotificationType::VoiceLeave;

    if (m_settings.renderImages && user.avatar.hasValue()) {
        data.iconUrl = Discord::Cdn::userAvatar(userId, user.avatar.get(), 64).toString();
    }

    if (channel.guildId.hasValue()) {
        auto guild = getGuild(channel.guildId.get());
        if (guild) {
            data.body += QString(" in %1").arg(guild->name);
            data.guildName = guild->name;
            data.guildId = guild->id.get().toString();
        }
    }

    data.onClick = [this, channelId]() {
        if (m_instance) {
            emit openChannelRequested(channelId);
        }
    };

    data.onIconClick = [this, userId]() {
        if (m_instance) {
            emit openUserProfileRequested(userId);
        }
    };

    return data;
}

Notification::ToastNotificationData NotificationManager::createRelationshipNotification(const Discord::Relationship &relationship)
{
    Notification::ToastNotificationData data;
    if (relationship.user.hasValue()) {
        const auto &user = relationship.user.get();
        data.iconUrl = Discord::Cdn::userAvatar(user.id.get(), user.avatar.getOr(""), 64).toString();
    }
    data.badgeColor = Notification::generateBadgeColor(relationship.id.get().toString());
    data.type = Notification::NotificationType::FriendRequest;

    if (relationship.type.get() == Discord::RelationshipType::FRIEND) {
        const auto &user = relationship.user.get();
        data.title = QString("%1 is now your friend").arg(getUserDisplayName(user));
        data.body = "You can now message them directly.";
        data.type = Notification::NotificationType::FriendAccepted;
        data.onClick = [this, userId = user.id.get()]() {
            emit openDmWithUserRequested(userId);
        };
    } else if (relationship.type.get() == Discord::RelationshipType::INCOMING_REQUEST) {
        const auto &user = relationship.user.get();
        data.title = QString("Incoming friend request from %1").arg(getUserDisplayName(user));
        data.body = "Open friends tab to accept or decline.";
        data.onClick = [this]() {
            emit openFriendsTabRequested();
        };
    }

    if (relationship.user.hasValue()) {
        data.onIconClick = [this, userId = relationship.user->id.get()]() {
        if (m_instance) {
            emit openUserProfileRequested(userId);
        }
        };
    }

    return data;
}

QString NotificationManager::getUserDisplayName(const Discord::User &user) const
{
    if (user.globalName.hasValue() && !user.globalName->isEmpty())
        return *user.globalName;
    return user.username;
}

QString NotificationManager::getUserDisplayName(const Discord::Member &member) const
{
    if (member.nick.hasValue() && !member.nick->isEmpty())
        return *member.nick;
    if (member.user.hasValue())
        return getUserDisplayName(member.user.get());
    return "Unknown User";
}

bool NotificationManager::isMentioned(const Discord::Message &message) const
{
    if (!m_cachedCurrentUser.has_value()) return false;
    Core::Snowflake myId = m_cachedCurrentUser->id.get();

    // Direct mention
    if (message.mentions.hasValue()) {
        for (const auto &mention : message.mentions.get()) {
            if (mention.id.get() == myId) return true;
        }
    }

    // @everyone / @here mentions
    const auto content = message.content.get();
    if (content.contains("@everyone") || content.contains("@here")) {
        return true;
    }

    if (message.mentionRoles.hasValue() && message.guildId.hasValue() && m_instance) {
        const auto myRoles = m_instance->getMemberRolesSorted(message.guildId.get(), myId);
        if (!myRoles.isEmpty()) {
            QSet<Core::Snowflake> mentionedRoles(message.mentionRoles->begin(),
                                                 message.mentionRoles->end());
            for (const auto &role : myRoles) {
                if (mentionedRoles.contains(role.id.get()))
                    return true;
            }
        }
    }

    return content.contains(QString("<@%1>").arg(myId.toString()));
}

Discord::MessageNotificationLevel NotificationManager::getChannelNotificationLevel(const Discord::Channel &channel) const
{
    if (!channel.guildId.hasValue()) return Discord::MessageNotificationLevel::NO_MESSAGES;

    using Level = Discord::MessageNotificationLevel;

    const Core::Snowflake guildId = channel.guildId.get();
    const Core::Snowflake channelId = channel.id.get();

    if (m_instance) {
        if (auto *rs = m_instance->readState()) {
            // Muted channels produce no notifications
            if (rs->isChannelMuted(channelId))
                return Level::NO_MESSAGES;

            // Muted guild drops to mentions-only
            if (rs->isGuildMuted(guildId))
                return Level::ONLY_MENTIONS;

            // Check per-channel overrides from UserGuildSettings,
            // then parent-category override, then guild-level setting.
            if (auto *gs = rs->getGuildSettings(guildId)) {
                // Channel-specific override
                if (gs->channelOverrides.hasValue()) {
                    for (const auto &override : gs->channelOverrides.get()) {
                        if (override.channelId.get() == channelId &&
                            override.messageNotifications.hasValue() &&
                            override.messageNotifications.get() != Level::INHERIT) {
                            return override.messageNotifications.get();
                        }
                    }

                    // Parent category override (e.g. for text channels inside a category)
                    if (channel.parentId.hasValue()) {
                        for (const auto &override : gs->channelOverrides.get()) {
                            if (override.channelId.get() == channel.parentId.get() &&
                                override.messageNotifications.hasValue() &&
                                override.messageNotifications.get() != Level::INHERIT) {
                                return override.messageNotifications.get();
                            }
                        }
                    }
                }

                // Guild-wide notification setting
                if (gs->messageNotifications.hasValue() &&
                    gs->messageNotifications.get() != Level::INHERIT) {
                    return gs->messageNotifications.get();
                }
            }
        }
    }

    // Final fallback: guild's default notification level
    if (auto guild = getGuild(guildId)) {
        if (guild->defaultMessageNotifications.hasValue())
            return guild->defaultMessageNotifications.get();
    }

    // Absolute fallback
    return Level::ONLY_MENTIONS;
}

std::optional<Discord::Guild> NotificationManager::getGuild(Core::Snowflake guildId) const
{
    if (!m_instance) return std::nullopt;
    return m_instance->getGuild(guildId);
}

std::optional<Discord::User> NotificationManager::getUser(Core::Snowflake userId) const
{
    if (!m_instance) return std::nullopt;
    return m_instance->users()->getUser(userId);
}

NotificationManager::StreamerModeState NotificationManager::evaluateStreamerMode()
{
    StreamerModeState state;

    if (m_settings.disableInStreamerMode && m_streamerModeEnabled) {
        state.shouldIgnore = true;
        return state;
    }

    if (m_isStreaming) {
        switch (m_settings.streamingTreatment) {
        case Notification::NotificationSettings::StreamingTreatment::Ignore:
            state.shouldIgnore = true;
            break;
        case Notification::NotificationSettings::StreamingTreatment::NoContent:
            state.shouldRedact = true;
            break;
        case Notification::NotificationSettings::StreamingTreatment::Normal:
        default:
            break;
        }
    }

    return state;
}

bool NotificationManager::isOwnPresenceDnd() const
{
    return m_instance && m_instance->presenceStatus() == QStringLiteral("dnd");
}

void NotificationManager::checkStreamerMode()
{
    if (!m_instance || !m_instance->discord()) return;

    const QSettings settings;
    const bool manualEnabled = settings.value("streamer/enabled", false).toBool();
    const bool autoDetect = settings.value("streamer/auto_detect", false).toBool();
    const bool detected = autoDetect && streamingSoftwareRunning();

    m_streamerModeEnabled = manualEnabled || detected;
    m_isStreaming = m_streamerModeEnabled;
}

bool NotificationManager::shouldPlaySoundForType(const Notification::ToastNotificationData &data) const
{
    switch (data.type) {
    case Notification::NotificationType::DirectMessage:
        return m_settings.soundForDMs;
    case Notification::NotificationType::GroupMessage:
        return m_settings.soundForGroupDMs;
    case Notification::NotificationType::Mention:
        return m_settings.soundForMentions;
    case Notification::NotificationType::FriendRequest:
    case Notification::NotificationType::FriendAccepted:
        return m_settings.soundForFriendRequests;
    case Notification::NotificationType::Message:
        return m_settings.soundForFriendServerMessages;
    case Notification::NotificationType::VoiceJoin:
    case Notification::NotificationType::VoiceLeave:
    case Notification::NotificationType::VoiceMove:
    case Notification::NotificationType::Custom:
    default:
        return true;
    }
}

QString NotificationManager::selectSoundForNotification(const Notification::ToastNotificationData &data)
{
    // Check user-specific sound first
    if (data.authorId.isValid()) {
        auto userSound = getUserSound(data.authorId.toString());
        if (userSound.enabled) {
            bool shouldUse = false;
            if (m_lastNotificationContext.has_value()) {
                const auto &ctx = m_lastNotificationContext.value();
                shouldUse = ctx.userOnNotifyList || ctx.isDM || ctx.channelOnNotifyList;
            }
            if (shouldUse) {
                return userSound.selectedSound;
            }
        }
    }

    // Check channel-specific override
    if (!data.channelId.isEmpty()) {
        auto override = getSoundOverride(data.channelId);
        if (override.enabled) {
            return override.selectedSound;
        }
    }

    // Default based on notification type
    switch (data.type) {
    case Notification::NotificationType::Mention:
        return SoundManager::Mention1;
    case Notification::NotificationType::DirectMessage:
        return SoundManager::Message3;
    case Notification::NotificationType::GroupMessage:
        return SoundManager::Message3;
    case Notification::NotificationType::FriendRequest:
    case Notification::NotificationType::FriendAccepted:
        return SoundManager::DefaultNotification;
    case Notification::NotificationType::VoiceJoin:
    case Notification::NotificationType::VoiceLeave:
    case Notification::NotificationType::VoiceMove:
        return SoundManager::DefaultNotification;
    case Notification::NotificationType::Message:
    default:
        return SoundManager::Message1;
    }
}

QSet<QString> NotificationManager::notifyForList() const
{
    QMutexLocker locker(&m_notifyListMutex);
    return m_notifyForSet;
}

QSet<QString> NotificationManager::ignoreUsersList() const
{
    QMutexLocker locker(&m_notifyListMutex);
    return m_ignoreUsersSet;
}

void NotificationManager::addToNotifyList(const QString &id)
{
    QMutexLocker locker(&m_notifyListMutex);
    m_notifyForSet.insert(id);
    saveSettings();
}

void NotificationManager::removeFromNotifyList(const QString &id)
{
    QMutexLocker locker(&m_notifyListMutex);
    m_notifyForSet.remove(id);
    saveSettings();
}

void NotificationManager::addToIgnoreList(const QString &id)
{
    QMutexLocker locker(&m_notifyListMutex);
    m_ignoreUsersSet.insert(id);
    saveSettings();
}

void NotificationManager::removeFromIgnoreList(const QString &id)
{
    QMutexLocker locker(&m_notifyListMutex);
    m_ignoreUsersSet.remove(id);
    saveSettings();
}

Notification::SoundOverride NotificationManager::getSoundOverride(const QString &soundId) const
{
    return m_soundOverrides.value(soundId, Notification::SoundOverride{});
}

void NotificationManager::setSoundOverride(const QString &soundId, const Notification::SoundOverride &override)
{
    m_soundOverrides[soundId] = override;
    saveSettings();
    emit soundOverrideChanged(soundId);
}

Notification::UserSoundMapping NotificationManager::getUserSound(const QString &userId) const
{
    return m_userSounds.value(userId, Notification::UserSoundMapping{});
}

void NotificationManager::setUserSound(const QString &userId, const Notification::UserSoundMapping &sound)
{
    if (sound.enabled) {
        m_userSounds[userId] = sound;
    } else {
        m_userSounds.remove(userId);
    }
    saveSettings();
    emit userSoundChanged(userId);
}

void NotificationManager::sendTestNotification()
{
    Notification::ToastNotificationData data;
    data.title = QStringLiteral("Acheron (#test-channel)");
    data.body = "This is a test notification from Acheron.";
    data.attachments = 0;
    data.channelName = "test-channel";
    data.badgeColor = Notification::generateBadgeColor(QStringLiteral("acheron-test"));
    data.type = Notification::NotificationType::Custom;
    data.onClick = []() {};

    if (m_instance && m_instance->discord()) {
        const auto &me = m_instance->discord()->getMe();
        if (me.id.get().isValid()) {
            data.title = QString("%1 (#test-channel)").arg(getUserDisplayName(me));
            data.iconUrl = Discord::Cdn::userAvatar(me.id.get(), me.avatar.getOr(""), 64).toString();
            data.badgeColor = Notification::generateBadgeColor(me.id.get().toString());
        }
    }

    showNotification(data);
}

void NotificationManager::setStreamerModeEnabled(bool enabled)
{
    m_streamerModeEnabled = enabled;
    m_isStreaming = enabled;
}

void NotificationManager::showNativeNotification(const Notification::ToastNotificationData &data)
{
    QString errorMessage;
    if (!ensureNativeNotificationTray(&errorMessage)) {
        qWarning() << "Native notifications are unavailable:" << errorMessage;
        return;
    }

    QString title = data.title;
    QString message = data.body;
    if (message.length() > 256) {
        message.truncate(253);
        message += "...";
    }

    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;
    if (data.type == Notification::NotificationType::Mention) {
        icon = QSystemTrayIcon::Warning;
    }

    m_systemTray->showMessage(title, message, icon, m_settings.timeoutSeconds * 1000);
}

bool NotificationManager::requestNativeNotificationPermission(QString *errorMessage)
{
    if (!ensureNativeNotificationTray(errorMessage))
        return false;

    m_systemTray->showMessage(QStringLiteral("Acheron notifications enabled"),
                              QStringLiteral("Native notification permission test sent."),
                              QSystemTrayIcon::Information,
                              5000);
    return true;
}

bool NotificationManager::ensureNativeNotificationTray(QString *errorMessage)
{
    auto setError = [errorMessage](const QString &message) {
        if (errorMessage)
            *errorMessage = message;
    };

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        setError(QStringLiteral("The operating system notification tray is not available."));
        return false;
    }

    if (!QSystemTrayIcon::supportsMessages()) {
        setError(QStringLiteral("This desktop environment does not support tray notifications."));
        return false;
    }

    if (!m_systemTray) {
        m_systemTray = new QSystemTrayIcon(this);
        m_systemTray->setIcon(qApp->windowIcon());
        m_systemTray->setToolTip(QStringLiteral("Acheron"));
    }

    if (!m_systemTray->isVisible())
        m_systemTray->setVisible(true);

    return true;
}

} // namespace Core
} // namespace Acheron

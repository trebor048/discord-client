#include "NotificationController.hpp"

#include <optional>

#include "MainWindow.hpp"
#include "Settings/SettingsWindow.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Notification/NotificationManager.hpp"

namespace Acheron {
namespace UI {

NotificationController::NotificationController(MainWindow *window)
    : QObject(window), m_window(window)
{
}

void NotificationController::teardown()
{
    if (notificationManager) {
        notificationManager->deleteLater();
        notificationManager = nullptr;
    }
}

void NotificationController::setupForInstance(Core::ClientInstance *instance)
{
    // Initialize notification manager for this instance
    notificationManager = new Core::NotificationManager(instance, m_window);
    notificationManager->initialize();

    // Clear active channel when switching instances
    notificationManager->setActiveChannel(Core::Snowflake::Invalid);

    // Connect notification navigation signals
    connect(notificationManager, &Core::NotificationManager::openChannelRequested,
            m_window, [this](Core::Snowflake channelId) {
        if (m_window->currentInstance) {
            // Find and switch to the channel
            m_window->selectChannelInTree(channelId);
        }
    });
    connect(notificationManager, &Core::NotificationManager::openUserProfileRequested,
            m_window, [this](Core::Snowflake userId) {
        if (m_window->currentInstance) {
            // Open user profile dialog
            m_window->showUserProfile(userId, Core::Snowflake::Invalid);
        }
    });
    connect(notificationManager, &Core::NotificationManager::openDmWithUserRequested,
            m_window, [this](Core::Snowflake userId) {
        if (m_window->currentInstance) {
            std::optional<Core::Snowflake> dmChannelId = m_window->currentInstance->findDmChannelWithUser(userId);
            if (dmChannelId.has_value()) {
                m_window->selectChannelInTree(*dmChannelId);
            }
        }
    });
    connect(notificationManager, &Core::NotificationManager::openFriendsTabRequested,
            m_window, [this]() {
        m_window->openFriendsWindow();
    });
}

void NotificationController::setActiveChannel(Core::Snowflake channelId)
{
    if (notificationManager) {
        notificationManager->setActiveChannel(channelId);
    }
}

void NotificationController::setStreamerModeEnabled(bool enabled)
{
    if (notificationManager) {
        notificationManager->setStreamerModeEnabled(enabled);
    }
}

void NotificationController::reloadSettings()
{
    if (notificationManager) {
        notificationManager->loadSettings();
    }
}

void NotificationController::applyToSettingsWindow(SettingsWindow *settingsWindow)
{
    if (notificationManager) {
        settingsWindow->setNotificationManager(notificationManager);
    }
}

} // namespace UI
} // namespace Acheron

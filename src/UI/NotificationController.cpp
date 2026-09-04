#include "NotificationController.hpp"

#include <optional>

#include <QPointer>
#include <QTimer>

#include "MainWindow.hpp"
#include "Settings/SettingsWindow.hpp"
#include "Settings/NotificationsPage.hpp"
#include "Core/ClientInstance.hpp"
#include "Core/Session.hpp"
#include "Core/ImageManager.hpp"
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
    // Replace any existing manager so repeated setup calls don't leak or
    // duplicate signal subscriptions.
    if (notificationManager) {
        disconnect(notificationManager, nullptr, nullptr, nullptr);
        notificationManager->deleteLater();
        notificationManager = nullptr;
    }

    // Initialize notification manager for this instance
    notificationManager = new Core::NotificationManager(instance, m_window);
    if (m_window->session) {
        notificationManager->setImageManager(m_window->session->getImageManager());
    }
    notificationManager->setInWindowParent(m_window);
    notificationManager->initialize();

    // Clear active channel when switching instances
    notificationManager->setActiveChannel(Core::Snowflake::Invalid);

    // A toast click should surface the client window before navigating so the
    // jump is actually visible, even when the window is minimized or buried.
    const auto bringToForeground = [this]() {
        if (m_window->isMinimized())
            m_window->showNormal();
        m_window->raise();
        m_window->activateWindow();

        // Win11 activation rules can keep raise() from surfacing the window
        // above a focused foreign app, so briefly re-apply the top-most hint
        // (pop above everything), then drop it so normal stacking resumes.
        if (!(m_window->windowFlags() & Qt::WindowStaysOnTopHint)) {
            const QPointer<MainWindow> guard(m_window);
            m_window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
            m_window->show();
            QTimer::singleShot(400, guard, [guard]() {
                if (!guard)
                    return;
                guard->setWindowFlag(Qt::WindowStaysOnTopHint, false);
                guard->show();
                guard->raise();
                guard->activateWindow();
            });
        }
    };

    // Connect notification navigation signals
    connect(notificationManager, &Core::NotificationManager::openChannelRequested,
            m_window, [this, bringToForeground](Core::Snowflake channelId) {
        if (m_window->currentInstance) {
            bringToForeground();
            // Find and switch to the channel
            m_window->selectChannelInTree(channelId);
        }
    });
    connect(notificationManager, &Core::NotificationManager::jumpToMessageRequested,
            m_window, [this, bringToForeground](Core::Snowflake channelId, Core::Snowflake messageId) {
        if (m_window->currentInstance) {
            bringToForeground();
            m_window->jumpToMessage(channelId, messageId);
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
            m_window, [this, bringToForeground](Core::Snowflake userId) {
        if (m_window->currentInstance) {
            bringToForeground();
            std::optional<Core::Snowflake> dmChannelId = m_window->currentInstance->findDmChannelWithUser(userId);
            if (dmChannelId.has_value()) {
                m_window->selectChannelInTree(*dmChannelId);
            }
        }
    });
    connect(notificationManager, &Core::NotificationManager::openFriendsTabRequested,
            m_window, [this, bringToForeground]() {
        bringToForeground();
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

        // The settings page persists to QSettings on every change; reload the
        // manager so edits take effect immediately.
        if (auto *page = settingsWindow->findChild<NotificationsPage *>()) {
            connect(page, &NotificationsPage::settingsChanged,
                    notificationManager, &Core::NotificationManager::loadSettings,
                    Qt::UniqueConnection);
        }
    }
}

} // namespace UI
} // namespace Acheron

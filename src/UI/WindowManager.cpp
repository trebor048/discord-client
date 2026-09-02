#include "WindowManager.hpp"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <QScreen>
#include <QSettings>
#include <QSplitter>

#include "MainWindow.hpp"
#include "NotificationController.hpp"
#include "VoiceStateController.hpp"
#include "Accounts/AccountsWindow.hpp"
#include "Accounts/AccountsModel.hpp"
#include "Settings/SettingsWindow.hpp"
#include "Settings/NotificationsPage.hpp"
#include "FriendsPage.hpp"
#include "Dialogs/EditProfileDialog.hpp"
#include "Dialogs/UserProfilePopup.hpp"
#include "Chat/ChatView.hpp"
#include "Input/MessageInput.hpp"
#include "Core/Session.hpp"
#include "Core/ClientInstance.hpp"

namespace Acheron {
namespace UI {

namespace {

QList<QPointer<MainWindow>> openMainWindows;

QList<MainWindow *> visibleMainWindows()
{
    QList<MainWindow *> windows;
    for (auto it = openMainWindows.begin(); it != openMainWindows.end();) {
        if (it->isNull()) {
            it = openMainWindows.erase(it);
            continue;
        }

        MainWindow *window = it->data();
        if (window->isVisible())
            windows.append(window);
        ++it;
    }
    return windows;
}

} // namespace

WindowManager::WindowManager(MainWindow *window)
    : QObject(window), m_window(window)
{
}

void WindowManager::trackWindow(MainWindow *window)
{
    openMainWindows.append(window);
}

void WindowManager::openAccountsWindow()
{
    if (!accountsWindow) {
        accountsWindow = new AccountsWindow(m_window->session, m_window->accountsModel, m_window);
    }

    accountsWindow->show();
    accountsWindow->raise();
    accountsWindow->activateWindow();
}

void WindowManager::openSettingsWindow()
{
    if (!settingsWindow) {
        settingsWindow = new SettingsWindow(m_window);
        connect(settingsWindow, &SettingsWindow::channelListModeChanged, m_window, [window = m_window](bool classic) {
            window->setChannelListMode(classic ? MainWindow::ChannelListMode::Classic
                                               : MainWindow::ChannelListMode::Tree);
        });
        connect(settingsWindow, &SettingsWindow::compactModeChanged, m_window->chatView, &ChatView::setCompactMode);
        connect(settingsWindow, &SettingsWindow::compactInputChanged, m_window->messageInput, &MessageInput::setCompact);
        connect(settingsWindow, &SettingsWindow::newTabBehaviorChanged, m_window, []() {
            // no-op: behavior is read on demand
        });
        connect(settingsWindow, &SettingsWindow::showTimestampsChanged, m_window->chatView,
                &ChatView::setShowTimestamps);
        connect(settingsWindow, &SettingsWindow::notificationSoundsChanged, m_window,
                [](bool enabled) {
                    // Notification sounds are now managed by NotificationManager
                    Q_UNUSED(enabled);
                });
        connect(settingsWindow, &SettingsWindow::customStatusChanged, m_window,
                &MainWindow::applyCustomStatus);
        connect(settingsWindow, &SettingsWindow::streamerModeChanged, m_window, [this](bool enabled) {
            QSettings().setValue("streamer/enabled", enabled);
            m_window->notificationController->setStreamerModeEnabled(enabled);
        });
        connect(settingsWindow, &SettingsWindow::editProfileRequested, m_window, [this]() {
            if (m_window->currentInstance && m_window->currentInstance->discord()) {
                auto *dialog = new EditProfileDialog(m_window->currentInstance->discord(), m_window);
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->exec();
            }
        });
        connect(settingsWindow, &SettingsWindow::pushToTalkToggled, m_window, [this](bool enabled) {
            m_window->voiceController->setPushToTalkEnabledForAll(enabled);
        });
        connect(settingsWindow, &SettingsWindow::pushToTalkKeyChanged, m_window, [this](const QString &key) {
            m_window->voiceController->setPushToTalkKey(key);
        });
    }

    // Propagate the Discord client to settings pages that need it
    if (m_window->currentInstance && m_window->currentInstance->discord()) {
        settingsWindow->setClient(m_window->currentInstance->discord());
    }

    settingsWindow->setImageManager(m_window->session->getImageManager());

    if (m_window->currentInstance) {
        settingsWindow->setVoiceManager(m_window->currentInstance->voice());
    }

    // Connect notification manager to settings page
    m_window->notificationController->applyToSettingsWindow(settingsWindow);

    settingsWindow->show();
    settingsWindow->raise();
    settingsWindow->activateWindow();
}

void WindowManager::openSettingsWindow(const QString &page)
{
    openSettingsWindow();
    if (settingsWindow && !page.isEmpty())
        settingsWindow->selectPage(page);
}

void WindowManager::openFriendsWindow()
{
    if (!m_window->currentInstance)
        return;

    if (friendsWindow) {
        friendsWindow->raise();
        friendsWindow->activateWindow();
        return;
    }

    friendsWindow = new FriendsPage(m_window->currentInstance, m_window);
    friendsWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(friendsWindow, &QObject::destroyed, this, [this]() { friendsWindow = nullptr; });
    connect(friendsWindow, &FriendsPage::openDmRequested, m_window,
            [this](Core::Snowflake userId) {
                if (!m_window->currentInstance)
                    return;
                if (auto dmChannelId = m_window->currentInstance->findDmChannelWithUser(userId); dmChannelId.has_value())
                    m_window->selectChannelInTree(*dmChannelId);
            });
    connect(friendsWindow, &FriendsPage::openUserProfileRequested, m_window,
            [this](Core::Snowflake userId) {
                if (m_window->currentInstance)
                    showUserProfile(userId, Core::Snowflake::Invalid);
            });
    friendsWindow->show();
    friendsWindow->raise();
    friendsWindow->activateWindow();
}

void WindowManager::showUserProfile(Core::Snowflake userId, Core::Snowflake guildId)
{
    if (!m_window->currentInstance)
        return;
    (new UserProfilePopup(m_window->session->getImageManager(), m_window->currentInstance, userId,
                          guildId, m_window))
            ->show();
}

void WindowManager::populateWindowMenu()
{
    if (!m_window->windowMenu)
        return;

    m_window->windowMenu->clear();

    const QList<MainWindow *> windows = visibleMainWindows();
    const bool hasMultipleWindows = windows.size() > 1;

    QAction *mergeAction = m_window->windowMenu->addAction(tr("&Merge All Windows"));
    mergeAction->setEnabled(hasMultipleWindows);
    connect(mergeAction, &QAction::triggered, m_window, &MainWindow::mergeAllWindows);

    QAction *closeAllAction = m_window->windowMenu->addAction(tr("&Close All Windows"));
    closeAllAction->setEnabled(!windows.isEmpty());
    connect(closeAllAction, &QAction::triggered, m_window, &MainWindow::closeAllWindows);

    m_window->windowMenu->addSeparator();

    QWidget *activeWindow = QApplication::activeWindow();
    for (int i = 0; i < windows.size(); ++i) {
        MainWindow *window = windows[i];
        QString title = window->currentChannelEntry().name;
        if (title.isEmpty())
            title = tr("Window %1").arg(i + 1);
        if (window->windowManager->isDetachedWindow)
            title = tr("%1 (Detached)").arg(title);

        QAction *action = m_window->windowMenu->addAction(title);
        action->setCheckable(true);
        action->setChecked(window == activeWindow || window->isActiveWindow());
        connect(action, &QAction::triggered, m_window, [w = QPointer<MainWindow>(window)]() {
            // The detached window may have been closed and deleted (WA_DeleteOnClose),
            // which would make a raw capture a dangling pointer. QPointer nulls on
            // deletion, turning a stale menu entry into a harmless no-op.
            if (!w)
                return;
            w->show();
            w->raise();
            w->activateWindow();
        });
    }
}

void WindowManager::mergeAllWindows()
{
    const QList<MainWindow *> windows = visibleMainWindows();
    if (windows.size() <= 1)
        return;

    QList<TabEntry> merged = m_window->tabBar->tabEntries();
    const int activeIndex = m_window->tabBar->activeTabIndex();

    for (MainWindow *window : windows) {
        if (window == m_window)
            continue;

        const QList<TabEntry> entries = window->tabBar->tabEntries();
        for (const TabEntry &entry : entries) {
            if (entry.channelId.isValid())
                merged.append(entry);
        }
    }

    if (!merged.isEmpty()) {
        m_window->tabBar->restoreTabs(merged, activeIndex);
        m_window->activateChannel(m_window->tabBar->tabEntry(m_window->tabBar->activeTabIndex()));
    }

    m_window->show();
    m_window->raise();
    m_window->activateWindow();

    for (MainWindow *window : windows) {
        if (window != m_window) {
            window->windowManager->skipNextWindowStateSave = true;
            window->close();
        }
    }
}

void WindowManager::closeAllWindows()
{
    const QList<MainWindow *> windows = visibleMainWindows();
    for (MainWindow *window : windows)
        window->close();
}

TabEntry WindowManager::currentChannelEntry() const
{
    if (!m_window->tabBar || m_window->tabBar->tabCount() == 0)
        return {};
    return m_window->tabBar->tabEntry(m_window->tabBar->activeTabIndex());
}

void WindowManager::openChannelInNewWindow(const TabEntry &entry, bool tileToSide,
                                           Core::Snowflake jumpMessageId)
{
    if (!entry.channelId.isValid())
        return;

    auto *window = new MainWindow(m_window->session);
    // Detached windows are transient: free them when closed instead of leaking
    // one MainWindow per "open in new window" for the whole session. closeEvent
    // still runs first, so window-state persistence happens before destruction.
    window->setAttribute(Qt::WA_DeleteOnClose);
    // Mark as detached _before_ mutating tabs so the new window's
    // tabsChanged -> saveTabs path does not clobber the primary window's
    // persisted tabs (saveTabs guards on isDetachedWindow).
    window->setDetachedWindow(true);
    window->tabBar->restoreTabs({entry}, 0);
    window->activateChannel(entry);
    window->show();
    window->raise();
    window->activateWindow();

    // Scroll the new window to the message once its channel loads.
    if (jumpMessageId.isValid())
        window->jumpToMessage(entry.channelId, jumpMessageId);

    if (!tileToSide)
        return;

    QScreen *screen = m_window->windowHandle() ? m_window->windowHandle()->screen()
                                               : QGuiApplication::primaryScreen();
    QRect avail = screen ? screen->availableGeometry() : m_window->geometry();
    if (!avail.isValid())
        return;

    QRect leftHalf(avail.left(), avail.top(), avail.width() / 2, avail.height());
    QRect rightHalf(leftHalf.right() + 1, avail.top(), avail.width() - leftHalf.width(),
                    avail.height());
    m_window->resize(leftHalf.size());
    m_window->move(leftHalf.topLeft());
    window->resize(rightHalf.size());
    window->move(rightHalf.topLeft());
}

void WindowManager::openDetachedWindow(bool tileToSide)
{
    TabEntry entry = currentChannelEntry();
    if (!entry.channelId.isValid())
        return;
    openChannelInNewWindow(entry, tileToSide);
}

void WindowManager::setDetachedWindow(bool detached)
{
    isDetachedWindow = detached;
    if (m_window->leftSideWidget)
        m_window->leftSideWidget->setVisible(!detached);

    // In detached windows, the channel pane fills the entire space
    if (m_window->mainSplitter) {
        QList<int> sizes = m_window->mainSplitter->sizes();
        if (sizes.size() >= 2 && detached) {
            sizes[0] = 0;
            sizes[1] = sizes.size() > 1 ? sizes[1] + sizes[0] : 0;
            m_window->mainSplitter->setSizes(sizes);
        }
    }
}

} // namespace UI
} // namespace Acheron

#pragma once

#include <QObject>
#include <QString>

#include "Core/Snowflake.hpp"
#include "TabBar/TabBar.hpp"

namespace Acheron {
namespace Core {
class Session;
} // namespace Core
namespace UI {

class MainWindow;
class AccountsWindow;
class SettingsWindow;
class FriendsPage;

// Owns window-level management: the child windows (accounts/settings/friends),
// detached-window state, the window menu, and merge/close/detach orchestration
// across the tracked set of open MainWindows.
class WindowManager : public QObject
{
    Q_OBJECT
public:
    explicit WindowManager(MainWindow *window);

    // Track a freshly constructed MainWindow (mirrors the old static list).
    static void trackWindow(MainWindow *window);

    void openAccountsWindow();
    void openSettingsWindow();
    void openFriendsWindow();
    void showUserProfile(Core::Snowflake userId, Core::Snowflake guildId = Core::Snowflake::Invalid);

    void populateWindowMenu();
    void mergeAllWindows();
    void closeAllWindows();

    TabEntry currentChannelEntry() const;
    void openChannelInNewWindow(const TabEntry &entry, bool tileToSide);
    void openDetachedWindow(bool tileToSide);
    void setDetachedWindow(bool detached);

private:
    MainWindow *m_window = nullptr;

    AccountsWindow *accountsWindow = nullptr;
    SettingsWindow *settingsWindow = nullptr;
    FriendsPage *friendsWindow = nullptr;
    bool isDetachedWindow = false;
    bool skipNextWindowStateSave = false;

    friend class MainWindow;
};

} // namespace UI
} // namespace Acheron

#pragma once
#include <QMainWindow>
#include <QWidget>
#include <QLabel>
#include <QSplitter>
#include <QMenu>
#include <QCloseEvent>
#include <QEvent>
#include <QColor>
#include <QPoint>
#include <QElapsedTimer>
#include <QModelIndex>
#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>
#include <QSet>

#include <functional>

#include <Core/Snowflake.hpp>
#include "Input/MessageInput.hpp"
#include "MemberList/MemberListView.hpp"
#include "MemberList/MemberListModel.hpp"
#include "MemberList/MemberListDelegate.hpp"
#include "TabBar/TabBar.hpp"
#include "Discord/Entities.hpp"

#include "ChannelSelectionController.hpp"
#include "VoiceStateController.hpp"
#include "WindowManager.hpp"
#include "ContextMenuFactory.hpp"
#include "NotificationController.hpp"

namespace Acheron {
namespace Core {
class Session;
class ClientInstance;
class TypingTracker;
class NotificationManager;
namespace AV {
class PushToTalkListener;
}
} // namespace Core
namespace Discord {
struct TypingStart;
}
namespace UI {
class ChatView;
class ChatModel;
class ForumBrowser;
class ForumPostModel;
class ThreadBrowserPopup;
class ChannelTreeModel;
class ChannelFilterProxyModel;
class AccountsWindow;
class AccountsModel;
class SettingsWindow;
class FriendsPage;
class ChannelTreeView;
class ServerRailView;
class ServerRailModel;
struct ChannelNode;
class TypingIndicator;
class SlowModeIndicator;
class ConnectionBanner;
class PinnedMessagesPanel;
#ifndef ACHERON_NO_VOICE
class VoiceStatusBar;
#endif
} // namespace UI
} // namespace Acheron

namespace Acheron {
namespace UI {

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(Core::Session *session, QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onChannelSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void openForumPost(Core::Snowflake threadId, Core::Snowflake guildId);
    void onNewPostRequested();
    void onTypingStart(const Discord::TypingStart &event);
    void onChannelPermissionsChanged(Core::Snowflake channelId);

private:
    void switchActiveInstance(Core::ClientInstance *instance);
    void setupPermanentConnections(Core::ClientInstance *instance);
    void trackInstanceConnection(Core::ClientInstance *instance,
                                 const QMetaObject::Connection &connection);
    void disconnectInstanceConnections(Core::ClientInstance *instance);
    void switchToTabEntry(const TabEntry &entry);
    void activateChannel(const TabEntry &entry);
    void refreshTabReadStates();

    void saveWindowState();
    void restoreWindowState();
    void applyTreeState();
    void captureTreeState(QStringList &expanded,
                          QStringList &collapsed,
                          QStringList &collapsedCategories,
                          QSet<QString> &presentAccounts) const;
    void forEachSourceNode(const std::function<void(const QModelIndex &, ChannelNode *)> &fn) const;
    QString treeNodeKey(const ChannelNode *node) const;
    void maybeActivatePendingChannel(Core::Snowflake accountId);
    QColor resolveRoleColor(Core::Snowflake userId, Core::Snowflake guildId);
    void refreshGuildRoleData(Core::Snowflake guildId);
    void confirmAndLeaveGuild(Core::Snowflake accountId, Core::Snowflake guildId);
    void openGuildSettings(Core::Snowflake accountId, Core::Snowflake guildId);
    void showUserContextMenu(Core::Snowflake userId, Core::Snowflake guildId, QPoint globalPos);
    void selectChannelInTree(Core::Snowflake channelId);
    void jumpToMessage(Core::Snowflake channelId, Core::Snowflake messageId);
    void showUserProfile(Core::Snowflake userId, Core::Snowflake guildId = Core::Snowflake::Invalid);
    void openFriendsWindow();
    void applyCustomStatus(const QString &status);

public:
    enum class ChannelListMode {
        Tree,
        Classic
    };
    void setChannelListMode(ChannelListMode mode);
    void setDetachedWindow(bool detached);

private:
    QWidget *buildLeftSide();
    void onRailAccountHomeSelected(Core::Snowflake accountId);
    void onRailAccountHomeClicked(Core::Snowflake accountId);
    void onRailGuildSelected(Core::Snowflake accountId, Core::Snowflake guildId);
    void onRailGuildClicked(Core::Snowflake accountId, Core::Snowflake guildId);
    void selectInitialRailItem();
    void applyPendingRailSelection(Core::Snowflake accountId);
    Core::Snowflake resolveRailChannel(Core::Snowflake accountId, Core::Snowflake guildId);
    Core::Snowflake firstReadableChannel(Core::Snowflake accountId, Core::Snowflake guildId);
    bool channelReadable(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake channelId);
    void markIndexAsRead(Core::Snowflake accountId, const QModelIndex &sourceIndex);
    void recordLastViewedChannel(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake channelId);
    void recordRecentChannel(const TabEntry &entry);
#ifndef ACHERON_NO_VOICE
    void updateVoiceStatusLabel();
#endif

private:
    void setupUi();
    void setupMenu();
    void openDetachedWindow(bool tileToSide);
    void openChannelInNewWindow(const TabEntry &entry, bool tileToSide);
    void populateWindowMenu();
    void mergeAllWindows();
    void closeAllWindows();
    TabEntry currentChannelEntry() const;

    [[nodiscard]] TabEntry makeTabEntry(ChannelNode *node, ChannelNode *accountNode) const;

    void setViewMode(ChannelSelectionController::ViewMode mode);
    void setMemberListVisible(bool visible);
    void updateMemberListVisibility();
    void openForumChannel(Core::ClientInstance *instance, Core::Snowflake forumId, Core::Snowflake guildId);
    void applyChannelChrome(Core::ClientInstance *instance, Core::Snowflake channelId, const QString &name, bool isDm, Core::Snowflake guildId);
    void switchChatChannel(Core::Snowflake channelId, Core::Snowflake guildId);

    void openThreadBrowser();
    void navigateToChannel(Core::Snowflake channelId);
    void setThreadBrowserTarget(Core::Snowflake channelId);
    void openPinnedMessages();
    void setChannelName(const QString &name);
    void updateChannelNameElide();

    ChatView *chatView;
    ChatModel *chatModel;

    // forum/split state
    QSplitter *centerSplitter = nullptr;
    QWidget *threadPane = nullptr;
    QWidget *threadHeader = nullptr;
    QPushButton *popOutButton = nullptr;
    QPushButton *closeThreadButton = nullptr;
    ForumBrowser *forumBrowser = nullptr;
    ForumPostModel *forumModel = nullptr;

    ChannelTreeView *channelTree;
    ChannelTreeModel *channelTreeModel;
    ChannelFilterProxyModel *channelFilterProxy;

    ChannelListMode channelListMode = ChannelListMode::Tree;
    QWidget *leftSideWidget = nullptr;
    ServerRailView *serverRail = nullptr;
    ServerRailModel *serverRailModel = nullptr;
    QLabel *guildHeaderLabel = nullptr;
    QLabel *customStatusLabel = nullptr;

    AccountsModel *accountsModel;

    TabBar *tabBar;
    QWidget *channelToolbar = nullptr;
    QLabel *channelNameLabel = nullptr;
    QString channelFullName;
    QToolButton *threadBrowserButton = nullptr;
    QToolButton *pinnedMessagesButton = nullptr;
    ThreadBrowserPopup *threadBrowser = nullptr;
    PinnedMessagesPanel *pinnedMessagesPanel = nullptr;
    MessageInput *messageInput;
    TypingIndicator *typingIndicator;
    SlowModeIndicator *slowModeIndicator;
    ConnectionBanner *connectionBanner;
    MemberListView *memberListView;
    MemberListModel *memberListModel;
    Core::TypingTracker *typingTracker;

#ifndef ACHERON_NO_VOICE
    VoiceStatusBar *voiceStatusBar;
#endif

private slots:
    void openAccountsWindow();
    void openSettingsWindow();

private:
    Core::Session *session;
    Core::ClientInstance *currentInstance = nullptr;

    QSet<Core::Snowflake> instancesSignalsConnected;
    QHash<Core::Snowflake, QList<QMetaObject::Connection>> instanceConnections;
    QSplitter *mainSplitter = nullptr;
    QMenu *windowMenu = nullptr;
    bool notificationSoundsEnabled = true;
    QElapsedTimer lastNotificationTime;

    // same thing for expansion state
    bool hasSavedTreeState = false;
    QSet<QString> savedExpandedNodes;
    QSet<QString> savedCollapsedNodes;
    QSet<QString> savedCollapsedCategories;

    // also gotta wait for READY here
    bool hasSavedRailSelection = false;
    bool savedRailIsHome = true;
    Core::Snowflake savedRailAccountId;
    Core::Snowflake savedRailGuildId;

    // Extracted concerns. Each controller owns the logic + state of one slice
    // of the old monolith and reaches the shared widgets through a back-pointer.
    ChannelSelectionController *channelController = nullptr;
    VoiceStateController *voiceController = nullptr;
    WindowManager *windowManager = nullptr;
    ContextMenuFactory *contextMenuFactory = nullptr;
    NotificationController *notificationController = nullptr;

    friend class ChannelSelectionController;
    friend class VoiceStateController;
    friend class WindowManager;
    friend class ContextMenuFactory;
    friend class NotificationController;
};

} // namespace UI
} // namespace Acheron

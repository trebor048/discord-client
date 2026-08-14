#pragma once

#include <QObject>
#include <QModelIndex>
#include <QColor>
#include <QString>
#include <QList>
#include <QHash>

#include <optional>

#include "Core/Snowflake.hpp"
#include "TabBar/TabBar.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
} // namespace Core
namespace UI {

class MainWindow;
struct ChannelNode;

// Owns the logic and state behind selecting/activating a channel: tree
// selection, tab switching, view-mode/forum switching, the classic server-rail
// selection, read-state recording and role-color resolution. The widgets it
// drives stay owned by MainWindow; this controller reaches them through its
// MainWindow back-reference.
class ChannelSelectionController : public QObject
{
    Q_OBJECT
public:
    explicit ChannelSelectionController(MainWindow *window);

    enum class ViewMode {
        TextChannel,
        ForumBrowse,
        ForumSplit,
        ThreadPopout
    };

    void onChannelSelectionChanged(const QModelIndex &current, const QModelIndex &previous);
    void applyChannelChrome(Core::ClientInstance *instance, Core::Snowflake channelId,
                            const QString &name, bool isDm, Core::Snowflake guildId);
    void switchChatChannel(Core::Snowflake channelId, Core::Snowflake guildId);

    void setViewMode(ViewMode mode);
    void setMemberListVisible(bool visible);
    void updateMemberListVisibility();
    [[nodiscard]] TabEntry makeTabEntry(ChannelNode *node, ChannelNode *accountNode);
    void openForumChannel(Core::ClientInstance *instance, Core::Snowflake forumId, Core::Snowflake guildId);
    void openForumPost(Core::Snowflake threadId, Core::Snowflake guildId);
    void onNewPostRequested();

    void onRailGuildSelected(Core::Snowflake accountId, Core::Snowflake guildId);
    void onRailAccountHomeSelected(Core::Snowflake accountId);
    void onRailAccountHomeClicked(Core::Snowflake accountId);
    void selectInitialRailItem();
    void applyPendingRailSelection(Core::Snowflake accountId);
    void onRailGuildClicked(Core::Snowflake accountId, Core::Snowflake guildId);
    Core::Snowflake resolveRailChannel(Core::Snowflake accountId, Core::Snowflake guildId);
    bool channelReadable(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake channelId);
    Core::Snowflake firstReadableChannel(Core::Snowflake accountId, Core::Snowflake guildId);

    void markIndexAsRead(Core::Snowflake accountId, const QModelIndex &sourceIndex);
    void recordLastViewedChannel(Core::Snowflake accountId, Core::Snowflake guildId, Core::Snowflake channelId);
    void recordRecentChannel(const TabEntry &entry);

    void switchToTabEntry(const TabEntry &entry);
    void activateChannel(const TabEntry &entry);
    void refreshTabReadStates();
    void maybeActivatePendingChannel(Core::Snowflake accountId);

    QColor resolveRoleColor(Core::Snowflake userId, Core::Snowflake guildId);
    void refreshGuildRoleData(Core::Snowflake guildId);

    void selectChannelInTree(Core::Snowflake channelId);
    void navigateToChannel(Core::Snowflake channelId);

    void setThreadBrowserTarget(Core::Snowflake channelId);
    void openThreadBrowser();
    void openPinnedMessages();
    void updateChannelToolbarVisibility();

private:
    MainWindow *m_window = nullptr;

    ViewMode viewMode = ViewMode::TextChannel;
    bool memberListWanted = false;
    Core::Snowflake currentForumId;
    Core::Snowflake currentForumGuildId;

    Core::Snowflake cachedGuildId = Core::Snowflake::Invalid;
    QHash<Core::Snowflake, QColor> userColorCache; // current guild

    // restored-but-not-yet-activated channel. gotta wait for READY
    std::optional<TabEntry> pendingActiveEntry;

    Core::Snowflake threadBrowserChannelId = Core::Snowflake::Invalid;

    bool railHasSelection = false;
    bool railSelectedIsHome = false;
    Core::Snowflake railSelectedAccountId;
    Core::Snowflake railSelectedGuildId;
    // keyed "accountId:guildId"
    QHash<QString, Core::Snowflake> lastViewedChannel;
    QList<TabEntry> recentChannels;

    friend class MainWindow;
};

} // namespace UI
} // namespace Acheron

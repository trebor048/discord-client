#pragma once

#include <QWidget>
#include <QHash>
#include <QList>
#include <QRect>
#include <QShowEvent>
#include <QString>
#include <QUrl>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
class ImageManager;
}
namespace UI {

struct TabEntry
{
    Core::Snowflake channelId;
    Core::Snowflake guildId;
    Core::Snowflake accountId;
    QString name;
    QUrl iconUrl;
    bool isDm = false;
    bool pinned = false;
    bool isForum = false;

    bool operator==(const TabEntry &o) const { return channelId == o.channelId; }
};

struct Tab
{
    QList<TabEntry> history;
    int historyIndex = 0;

    const TabEntry &current() const { return history[historyIndex]; }
    bool canGoBack() const { return historyIndex > 0; }
    bool canGoForward() const { return historyIndex < history.size() - 1; }
};

class TabBar : public QWidget
{
    Q_OBJECT
public:
    explicit TabBar(Core::ImageManager *imageManager, QWidget *parent = nullptr);

    void updateCurrentTab(const TabEntry &entry);
    void openNewTab(const TabEntry &entry);

    void navigateBack();
    void navigateForward();

    bool canNavigateBack() const;
    bool canNavigateForward() const;

    int tabCount() const { return tabs.size(); }
    const TabEntry &tabEntry(int index) const { return tabs[index].current(); }
    QString activeTabName() const { return tabs.isEmpty() ? QString() : tabs[currentTabIndex].current().name; }

    int activeTabIndex() const { return currentTabIndex; }
    QList<TabEntry> tabEntries() const;
    void restoreTabs(const QList<TabEntry> &entries, int activeIndex);

    void updateChannelReadState(Core::Snowflake channelId, bool unread, int mentionCount,
                                int unreadCount);

    void setTabPinned(int index, bool pinned);
    bool isTabPinned(int index) const;

signals:
    void tabChanged(const TabEntry &entry);
    void tabsChanged();
    void addTabRequested();
    void readStateCleared(Core::Snowflake channelId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    struct TabRect
    {
        QRect tab;
        QRect closeBtn;
    };

    struct LayoutResult
    {
        QList<TabRect> rects;
        int visibleCount = 0;
        bool overflow = false;
    };

    LayoutResult computeTabLayout() const;
    QRect addButtonRect() const;
    QRect addButtonRect(bool overflowActive) const;
    QRect overflowButtonRect() const;
    QRect overflowButtonRect(bool overflowActive) const;
    int tabAtPos(const QPoint &pos) const;
    void closeTab(int index);
    void switchToTab(int index);
    void updateVisibility();

    void pinTabInternal(int index, bool pinned);
    int lastPinnedIndex() const;
    void showOverflowMenu();
    void showTabContextMenu(int index, const QPoint &globalPos);
    void reopenLastClosedTab();

    struct ChannelReadInfo
    {
        bool unread = false;
        int mentionCount = 0;
        int unreadCount = 0;
    };

    Core::ImageManager *imageManager;

    QHash<Core::Snowflake, ChannelReadInfo> channelReadStates;
    QList<Tab> tabs;
    int currentTabIndex = 0;
    int hoveredTab = -1;
    int hoveredClose = -1;
    bool hoveredAddButton = false;
    bool hoveredOverflowButton = false;

    bool dragging = false;
    int dragSourceIndex = -1;
    QPoint dragStartPos;

    QList<TabEntry> closedTabs;
    bool firstShow = true;

    // Layout cache — avoids recomputing on every paint/mouse/tooltip event
    mutable LayoutResult cachedLayout;
    mutable bool layoutDirty = true;

    constexpr static int TabHeight = 32;
    constexpr static int TabMaxWidth = 180;
    constexpr static int TabMinWidth = 60;
    constexpr static int PinnedTabWidth = 36;
    constexpr static int ChevronButtonWidth = 20;
    constexpr static int TabPadding = 8;
    constexpr static int CloseButtonSize = 14;
    constexpr static int AddButtonSize = 24;
    constexpr static int AddButtonMargin = 4;
    constexpr static int TabSpacing = 1;
    constexpr static int IconSize = 16;
    constexpr static int IconSpacing = 5;
    constexpr static int BadgeHeight = 16;
    constexpr static int BadgeMinWidth = 16;
    constexpr static int BadgePadding = 5;
    constexpr static int ClosedTabHistoryLimit = 20;

    /// Badge text with the 99+ cap shared by the tab badge paths.
    static QString capBadgeText(int count);
    /// Width of a badge given its text (min width honored).
    static int badgeWidth(const QFontMetrics &fm, const QString &text);
};

} // namespace UI
} // namespace Acheron

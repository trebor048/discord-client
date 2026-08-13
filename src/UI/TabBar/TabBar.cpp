#include "TabBar.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QFontMetrics>
#include <QApplication>
#include <QMenu>
#include <QAction>
#include <QToolTip>
#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QResizeEvent>

namespace Acheron {
namespace UI {

namespace {
QColor blendColors(const QColor &base, const QColor &toward, qreal ratio)
{
    qreal r = base.redF() * (1.0 - ratio) + toward.redF() * ratio;
    qreal g = base.greenF() * (1.0 - ratio) + toward.greenF() * ratio;
    qreal b = base.blueF() * (1.0 - ratio) + toward.blueF() * ratio;
    qreal a = base.alphaF() * (1.0 - ratio) + toward.alphaF() * ratio;
    QColor out;
    out.setRgbF(r, g, b, a);
    return out;
}
} // namespace

TabBar::TabBar(Core::ImageManager *imageManager, QWidget *parent)
    : QWidget(parent), imageManager(imageManager)
{
    setMouseTracking(true);
    setFixedHeight(TabHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    Tab initial;
    initial.history.append({ Core::Snowflake::Invalid, Core::Snowflake::Invalid,
                             Core::Snowflake::Invalid, QString() });
    tabs.append(initial);
    currentTabIndex = 0;

    connect(imageManager, &Core::ImageManager::imageFetched, this, [this](const QUrl &url, const QSize &, const QPixmap &) {
        for (const auto &tab : tabs) {
            if (tab.current().iconUrl == url) {
                update();
                return;
            }
        }
    });

    updateVisibility();
}

void TabBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (firstShow) {
        firstShow = false;
        Core::AnimationUtils::fadeIn(this, 300);
    }
}

void TabBar::resizeEvent(QResizeEvent *event)
{
    layoutDirty = true;
    QWidget::resizeEvent(event);
}

void TabBar::updateCurrentTab(const TabEntry &entry)
{
    if (!entry.channelId.isValid())
        return;

    Tab &tab = tabs[currentTabIndex];

    // dont push if already here
    if (!tab.history.isEmpty() && tab.current().channelId == entry.channelId)
        return;

    TabEntry stored = entry;

    // if the tab was at the initial empty state, replace it
    if (tab.history.size() == 1 && !tab.history[0].channelId.isValid()) {
        tab.history[0] = stored;
        tab.historyIndex = 0;
        update();
        return;
    }

    // preserve pinned state for navigation inside the same tab
    if (!tab.history.isEmpty())
        stored.pinned = tab.current().pinned;

    // truncate forward history
    while (tab.history.size() > tab.historyIndex + 1)
        tab.history.removeLast();

    tab.history.append(stored);
    tab.historyIndex = tab.history.size() - 1;

    constexpr static int MaxHistory = 50;
    if (tab.history.size() > MaxHistory) {
        int excess = tab.history.size() - MaxHistory;
        tab.history.erase(tab.history.begin(), tab.history.begin() + excess);
        tab.historyIndex -= excess;
    }

    update();
}

void TabBar::openNewTab(const TabEntry &entry)
{
    layoutDirty = true;

    Tab newTab;
    newTab.history.append(entry);
    newTab.historyIndex = 0;

    int insertAt = currentTabIndex + 1;
    // keep new tabs out of the pinned region
    int lastPin = lastPinnedIndex();
    if (entry.pinned && insertAt > lastPin + 1)
        insertAt = lastPin + 1;
    else if (!entry.pinned && insertAt <= lastPin)
        insertAt = lastPin + 1;

    tabs.insert(insertAt, newTab);
    currentTabIndex = insertAt;

    updateVisibility();
    update();
    emit tabChanged(entry);
}

QList<TabEntry> TabBar::tabEntries() const
{
    QList<TabEntry> out;
    out.reserve(tabs.size());
    for (const auto &tab : tabs)
        out.append(tab.current());
    return out;
}

void TabBar::restoreTabs(const QList<TabEntry> &entries, int activeIndex)
{
    if (entries.isEmpty())
        return;

    layoutDirty = true;
    tabs.clear();
    for (const auto &entry : entries) {
        Tab tab;
        tab.history.append(entry);
        tab.historyIndex = 0;
        tabs.append(tab);
    }

    currentTabIndex = qBound(0, activeIndex, tabs.size() - 1);

    updateVisibility();
    update();
}

void TabBar::setTabPinned(int index, bool pinned)
{
    if (index < 0 || index >= tabs.size())
        return;
    if (tabs[index].current().pinned == pinned)
        return;

    pinTabInternal(index, pinned);
}

bool TabBar::isTabPinned(int index) const
{
    return index >= 0 && index < tabs.size() && tabs[index].current().pinned;
}

void TabBar::pinTabInternal(int index, bool pinned)
{
    Tab tab = tabs.takeAt(index);
    for (auto &entry : tab.history)
        entry.pinned = pinned;

    int lastPin = lastPinnedIndex();
    int insertAt = lastPin + 1;

    tabs.insert(insertAt, tab);

    if (currentTabIndex == index)
        currentTabIndex = insertAt;
    else if (index < currentTabIndex && insertAt <= currentTabIndex)
        currentTabIndex++;
    else if (index > currentTabIndex && insertAt > currentTabIndex)
        currentTabIndex--;

    layoutDirty = true;
    update();
    emit tabChanged(tabs[currentTabIndex].current());
}

int TabBar::lastPinnedIndex() const
{
    for (int i = tabs.size() - 1; i >= 0; --i) {
        if (tabs[i].current().pinned)
            return i;
    }
    return -1;
}

void TabBar::navigateBack()
{
    Tab &tab = tabs[currentTabIndex];
    if (!tab.canGoBack())
        return;

    tab.historyIndex--;
    update();
    emit tabChanged(tab.current());
}

void TabBar::navigateForward()
{
    Tab &tab = tabs[currentTabIndex];
    if (!tab.canGoForward())
        return;

    tab.historyIndex++;
    update();
    emit tabChanged(tab.current());
}

bool TabBar::canNavigateBack() const
{
    return tabs[currentTabIndex].canGoBack();
}

bool TabBar::canNavigateForward() const
{
    return tabs[currentTabIndex].canGoForward();
}

void TabBar::updateChannelReadState(Core::Snowflake channelId, bool unread, int mentionCount)
{
    if (!channelId.isValid())
        return;

    auto it = channelReadStates.find(channelId);
    if (it != channelReadStates.end() && it->unread == unread && it->mentionCount == mentionCount)
        return;

    channelReadStates[channelId] = { unread, mentionCount };
    if (isVisible())
        update();
}

TabBar::LayoutResult TabBar::computeTabLayout() const
{
    if (!layoutDirty)
        return cachedLayout;

    LayoutResult result;
    if (tabs.isEmpty()) {
        cachedLayout = result;
        layoutDirty = false;
        return cachedLayout;
    }

    struct Desired
    {
        int preferred;
        int min;
        bool pinned;
        int badgeWidth;
    };

    QFontMetrics fm(font());
    QList<Desired> desired;
    desired.reserve(tabs.size());

    int totalPreferred = 0;
    for (int i = 0; i < tabs.size(); ++i) {
        const Tab &tab = tabs[i];
        const TabEntry &entry = tab.current();
        bool pinned = entry.pinned;
        int badgeW = 0;

        if (!pinned) {
            auto readIt = channelReadStates.constFind(entry.channelId);
            if (readIt != channelReadStates.constEnd() && readIt->mentionCount > 0) {
                QString badgeText = QString::number(readIt->mentionCount);
                int badgeTextW = fm.horizontalAdvance(badgeText);
                badgeW = qMax(BadgeMinWidth, badgeTextW + BadgePadding * 2);
            }
        }

        if (pinned) {
            desired.append({ PinnedTabWidth, PinnedTabWidth, true, 0 });
            totalPreferred += PinnedTabWidth;
        } else {
            QString label = entry.name;
            if (label.isEmpty())
                label = QStringLiteral("(no channel)");

            int textW = fm.horizontalAdvance(label);
            int tabW = TabPadding + IconSize + IconSpacing + textW + TabPadding;

            if (tabs.size() > 1)
                tabW += CloseButtonSize + 4;
            if (badgeW > 0)
                tabW += badgeW + 4;

            tabW = qBound(TabMinWidth, tabW, TabMaxWidth);
            desired.append({ tabW, TabMinWidth, false, badgeW });
            totalPreferred += tabW;
        }
    }

    totalPreferred += (tabs.size() - 1) * TabSpacing;

    int addBlock = AddButtonSize + AddButtonMargin * 2;
    int availNoOverflow = qMax(0, width() - addBlock);
    bool overflow = totalPreferred > availNoOverflow;
    int avail = overflow ? qMax(0, width() - addBlock - ChevronButtonWidth - 4) : availNoOverflow;

    // First pass: place as many tabs as fit.
    QList<int> widths;
    widths.reserve(tabs.size());
    int x = 0;
    int visibleCount = 0;
    for (int i = 0; i < tabs.size(); ++i) {
        int remaining = avail - x;
        int w = qMin(desired[i].preferred, remaining);
        if (w < desired[i].min) {
            overflow = true;
            break;
        }
        widths.append(w);
        x += w + TabSpacing;
        visibleCount++;
    }

    if (visibleCount == tabs.size())
        overflow = false;

    // Distribute unused space among normal tabs when in overflow.
    if (overflow && visibleCount > 0) {
        int widthUsed = 0;
        for (int i = 0; i < visibleCount; ++i)
            widthUsed += widths[i];
        widthUsed += (visibleCount - 1) * TabSpacing;

        int extra = avail - widthUsed;
        if (extra > 0) {
            QList<int> deficits;
            deficits.reserve(visibleCount);
            int totalDeficit = 0;
            for (int i = 0; i < visibleCount; ++i) {
                int deficit = desired[i].pinned ? 0 : (desired[i].preferred - widths[i]);
                deficits.append(deficit);
                totalDeficit += deficit;
            }

            if (totalDeficit > 0) {
                int distributed = 0;
                for (int i = 0; i < visibleCount; ++i) {
                    if (deficits[i] <= 0)
                        continue;
                    int add = extra * deficits[i] / totalDeficit;
                    widths[i] += add;
                    distributed += add;
                }
                int remainder = extra - distributed;
                for (int i = 0; i < visibleCount && remainder > 0; ++i) {
                    if (deficits[i] <= 0)
                        continue;
                    widths[i]++;
                    remainder--;
                }
            }
        }
    }

    // Build rects for visible tabs.
    x = 0;
    for (int i = 0; i < visibleCount; ++i) {
        const Desired &d = desired[i];
        int w = widths[i];
        QRect tabRect(x, 0, w, TabHeight);

        QRect closeRect;
        if (tabs.size() > 1 && !d.pinned) {
            int closeX = x + w - TabPadding - CloseButtonSize;
            int closeY = (TabHeight - CloseButtonSize) / 2;
            closeRect = QRect(closeX, closeY, CloseButtonSize, CloseButtonSize);
        }

        result.rects.append({ tabRect, closeRect });
        x += w + TabSpacing;
    }

    result.visibleCount = visibleCount;
    result.overflow = overflow && visibleCount < tabs.size();
    cachedLayout = result;
    layoutDirty = false;
    return cachedLayout;
}

QRect TabBar::addButtonRect() const
{
    LayoutResult layout = computeTabLayout();
    return addButtonRect(layout.overflow);
}

QRect TabBar::addButtonRect(bool overflowActive) const
{
    LayoutResult layout = computeTabLayout();
    int x = AddButtonMargin;
    if (!layout.rects.isEmpty())
        x = layout.rects.last().tab.right() + AddButtonMargin;
    if (overflowActive)
        x = qMin(x, width() - AddButtonSize - AddButtonMargin - ChevronButtonWidth - 4);
    else
        x = qMin(x, width() - AddButtonSize - AddButtonMargin);
    int y = (TabHeight - AddButtonSize) / 2;
    return QRect(x, y, AddButtonSize, AddButtonSize);
}

QRect TabBar::overflowButtonRect() const
{
    LayoutResult layout = computeTabLayout();
    return overflowButtonRect(layout.overflow);
}

QRect TabBar::overflowButtonRect(bool overflowActive) const
{
    if (!overflowActive)
        return QRect();

    QRect addRect = addButtonRect(true);
    int x = addRect.left() - ChevronButtonWidth - 4;
    int y = (TabHeight - ChevronButtonWidth) / 2;
    return QRect(x, y, ChevronButtonWidth, ChevronButtonWidth);
}

int TabBar::tabAtPos(const QPoint &pos) const
{
    LayoutResult layout = computeTabLayout();
    for (int i = 0; i < layout.rects.size(); ++i) {
        if (layout.rects[i].tab.contains(pos))
            return i;
    }
    return -1;
}

void TabBar::closeTab(int index)
{
    if (tabs.size() <= 1)
        return;
    if (index < 0 || index >= tabs.size())
        return;

    layoutDirty = true;

    TabEntry closed = tabs[index].current();
    if (closed.channelId.isValid()) {
        closedTabs.append(closed);
        while (closedTabs.size() > ClosedTabHistoryLimit)
            closedTabs.removeFirst();
    }

    bool wasActive = (index == currentTabIndex);

    tabs.removeAt(index);

    if (currentTabIndex >= tabs.size())
        currentTabIndex = tabs.size() - 1;
    else if (index < currentTabIndex)
        currentTabIndex--;

    updateVisibility();
    update();

    if (wasActive)
        emit tabChanged(tabs[currentTabIndex].current());
}

void TabBar::switchToTab(int index)
{
    if (index == currentTabIndex || index < 0 || index >= tabs.size())
        return;

    currentTabIndex = index;
    update();
    emit tabChanged(tabs[currentTabIndex].current());
}

void TabBar::updateVisibility()
{
    setVisible(true);
}

void TabBar::showOverflowMenu()
{
    LayoutResult layout = computeTabLayout();
    if (!layout.overflow)
        return;

    QMenu menu(this);
    for (int i = layout.visibleCount; i < tabs.size(); ++i) {
        const TabEntry &entry = tabs[i].current();
        QString label = entry.name.isEmpty() ? QStringLiteral("(no channel)") : entry.name;

        auto readIt = channelReadStates.constFind(entry.channelId);
        bool isUnread = readIt != channelReadStates.constEnd() && readIt->unread;
        int mentions = (readIt != channelReadStates.constEnd()) ? readIt->mentionCount : 0;

        if (mentions > 0)
            label = QStringLiteral("%1 (%2)").arg(label).arg(mentions);
        else if (isUnread)
            label = QStringLiteral("• %1").arg(label);

        QAction *action = new QAction(label, &menu);
        action->setCheckable(true);
        action->setChecked(i == currentTabIndex);

        if (isUnread) {
            QFont f = action->font();
            f.setBold(true);
            action->setFont(f);
        }

        if (!entry.iconUrl.isEmpty()) {
            QPixmap icon = imageManager->get(entry.iconUrl, QSize(IconSize * 2, IconSize * 2),
                                             Core::PinGroup::ChannelList);
            if (!icon.isNull())
                action->setIcon(QIcon(icon));
        }
        if (entry.pinned)
            action->setToolTip(tr("Pinned tab"));
        connect(action, &QAction::triggered, this, [this, i]() { switchToTab(i); });
        menu.addAction(action);
    }

    QRect rect = overflowButtonRect(true);
    menu.exec(mapToGlobal(QPoint(rect.left(), rect.bottom())));
}

void TabBar::showTabContextMenu(int index, const QPoint &globalPos)
{
    if (index < 0 || index >= tabs.size())
        return;

    QMenu menu(this);
    bool pinned = tabs[index].current().pinned;

    QAction *closeAction = new QAction(tr("Close"), &menu);
    closeAction->setEnabled(tabs.size() > 1 && !pinned);
    connect(closeAction, &QAction::triggered, this, [this, index]() { closeTab(index); });

    QAction *closeOthersAction = new QAction(tr("Close Others"), &menu);
    bool hasClosableOther = false;
    for (int i = 0; i < tabs.size(); ++i) {
        if (i != index && !tabs[i].current().pinned) {
            hasClosableOther = true;
            break;
        }
    }
    closeOthersAction->setEnabled(hasClosableOther);
    connect(closeOthersAction, &QAction::triggered, this, [this, index]() {
        for (int i = tabs.size() - 1; i >= 0; --i) {
            if (i != index && !tabs[i].current().pinned)
                closeTab(i);
        }
    });

    QAction *closeRightAction = new QAction(tr("Close Tabs to the Right"), &menu);
    bool hasClosableRight = false;
    for (int i = index + 1; i < tabs.size(); ++i) {
        if (!tabs[i].current().pinned) {
            hasClosableRight = true;
            break;
        }
    }
    closeRightAction->setEnabled(hasClosableRight);
    connect(closeRightAction, &QAction::triggered, this, [this, index]() {
        for (int i = tabs.size() - 1; i > index; --i) {
            if (!tabs[i].current().pinned)
                closeTab(i);
        }
    });

    const TabEntry &entry = tabs[index].current();
    auto readIt = channelReadStates.constFind(entry.channelId);
    bool hasReadState = readIt != channelReadStates.constEnd() && (readIt->unread || readIt->mentionCount > 0);
    if (hasReadState) {
        QAction *markReadAction = new QAction(tr("Mark as Read"), &menu);
        markReadAction->setIcon(QIcon::fromTheme(QStringLiteral("mail-mark-read")));
        connect(markReadAction, &QAction::triggered, this, [this, index]() {
            const TabEntry &e = tabs[index].current();
            updateChannelReadState(e.channelId, false, 0);
            emit readStateCleared(e.channelId);
        });
        menu.addAction(markReadAction);
        menu.addSeparator();
    }

    QAction *pinAction = new QAction(pinned ? tr("Unpin") : tr("Pin"), &menu);
    connect(pinAction, &QAction::triggered, this, [this, index, pinned]() {
        setTabPinned(index, !pinned);
    });

    QAction *reopenAction = new QAction(tr("Reopen Closed"), &menu);
    reopenAction->setEnabled(!closedTabs.isEmpty());
    connect(reopenAction, &QAction::triggered, this, &TabBar::reopenLastClosedTab);

    menu.addAction(closeAction);
    menu.addAction(closeOthersAction);
    menu.addAction(closeRightAction);
    menu.addSeparator();
    menu.addAction(pinAction);
    menu.addSeparator();
    menu.addAction(reopenAction);

    menu.exec(globalPos);
}

void TabBar::reopenLastClosedTab()
{
    if (closedTabs.isEmpty())
        return;

    TabEntry entry = closedTabs.takeLast();

    // if the only tab is the initial empty tab, replace it
    if (tabs.size() == 1 && !tabs[0].current().channelId.isValid()) {
        tabs[0].history.clear();
        tabs[0].history.append(entry);
        tabs[0].historyIndex = 0;
        currentTabIndex = 0;
        update();
        emit tabChanged(entry);
        return;
    }

    openNewTab(entry);
}

bool TabBar::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        auto *helpEvent = static_cast<QHelpEvent *>(event);
        LayoutResult layout = computeTabLayout();
        int idx = -1;
        for (int i = 0; i < layout.rects.size(); ++i) {
            if (layout.rects[i].tab.contains(helpEvent->pos())) {
                idx = i;
                break;
            }
        }

        if (idx >= 0) {
            QString text = tabs[idx].current().name;
            if (text.isEmpty())
                text = QStringLiteral("(no channel)");
            if (tabs[idx].current().pinned)
                text = tr("Pinned: %1").arg(text);
            QToolTip::showText(helpEvent->globalPos(), text, this);
        } else if (overflowButtonRect(layout.overflow).contains(helpEvent->pos())) {
            QToolTip::showText(helpEvent->globalPos(),
                               tr("More tabs (%1 hidden)").arg(tabs.size() - layout.visibleCount),
                               this);
        } else {
            QToolTip::hideText();
        }

        return true;
    }

    return QWidget::event(event);
}

void TabBar::contextMenuEvent(QContextMenuEvent *event)
{
    LayoutResult layout = computeTabLayout();
    int idx = -1;
    for (int i = 0; i < layout.rects.size(); ++i) {
        if (layout.rects[i].tab.contains(event->pos())) {
            idx = i;
            break;
        }
    }

    if (idx >= 0)
        showTabContextMenu(idx, event->globalPos());
    else
        event->ignore();
}

void TabBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette &pal = palette();
    QColor windowBg = pal.color(QPalette::Window);
    QColor altBase = pal.color(QPalette::AlternateBase);
    QColor highlight = pal.color(QPalette::Highlight);
    QColor textColor = pal.color(QPalette::Text);
    QColor dimText = textColor;
    dimText.setAlpha(140);

    p.fillRect(rect(), windowBg);

    p.setPen(QPen(altBase, 1));
    p.drawLine(0, height() - 1, width(), height() - 1);

    LayoutResult layout = computeTabLayout();
    QFontMetrics fm(font());

    for (int i = 0; i < layout.rects.size(); ++i) {
        const TabRect &tr = layout.rects[i];
        bool isActive = (i == currentTabIndex);
        bool isHovered = (i == hoveredTab);
        const TabEntry &entry = tabs[i].current();
        bool pinned = entry.pinned;

        QColor tabBg;
        if (isActive)
            tabBg = altBase;
        else if (isHovered)
            tabBg = blendColors(windowBg, highlight, 0.30);
        else
            tabBg = windowBg;

        QRect tabR = tr.tab.adjusted(0, 2, 0, 0);
        p.setPen(Qt::NoPen);
        p.setBrush(tabBg);
        p.drawRoundedRect(tabR.adjusted(0, 0, 0, 4), 4, 4);

        // active accent bar
        if (isActive) {
            p.setPen(Qt::NoPen);
            p.setBrush(highlight);
            p.drawRoundedRect(QRect(tabR.left() + 8, tabR.top(), tabR.width() - 16, 2), 1, 1);
        }

        // icon / initials
        int iconX = pinned ? tr.tab.left() + (tr.tab.width() - IconSize) / 2
                           : tr.tab.left() + TabPadding;
        int iconY = tr.tab.top() + (tr.tab.height() - IconSize) / 2;
        QRect iconRect(iconX, iconY, IconSize, IconSize);

        if (entry.isDm) {
            const qreal iconDpr = p.device() ? p.device()->devicePixelRatioF() : 1.0;
            const QPixmap dmIcon = Core::Theme::Icons::pixmap(Core::Theme::Icons::Name::AtSign,
                                                              IconSize,
                                                              isActive ? highlight : dimText,
                                                              iconDpr);
            p.drawPixmap(iconRect, dmIcon);
        } else if (!entry.iconUrl.isEmpty()) {
            QPixmap icon = imageManager->get(entry.iconUrl, QSize(IconSize * 2, IconSize * 2), Core::PinGroup::ChannelList);
            if (!icon.isNull()) {
                p.save();
                QPainterPath clipPath;
                clipPath.addRoundedRect(iconRect, 3, 3);
                p.setClipPath(clipPath);
                p.drawPixmap(iconRect, icon);
                p.restore();
            }
        }

        if (pinned) {
            QColor pinColor = isActive ? highlight : dimText;
            p.setPen(Qt::NoPen);
            p.setBrush(pinColor);
            QRect pinRect(tr.tab.center().x() - 2, tr.tab.bottom() - 7, 4, 4);
            p.drawEllipse(pinRect);

            auto readIt = channelReadStates.constFind(entry.channelId);
            bool isUnread = readIt != channelReadStates.constEnd() && readIt->unread;
            int mentions = (readIt != channelReadStates.constEnd()) ? readIt->mentionCount : 0;
            if (mentions > 0) {
                QString mentionText = QString::number(mentions);
                int badgeW = qMax(BadgeMinWidth, fm.horizontalAdvance(mentionText) + BadgePadding * 2);
                QRect badgeRect(tr.tab.right() - badgeW - 2, tr.tab.top() + 2, badgeW, BadgeHeight);
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xED, 0x42, 0x45));
                p.drawRoundedRect(badgeRect, BadgeHeight / 2, BadgeHeight / 2);
                p.save();
                QFont badgeFont = font();
                badgeFont.setPixelSize(10);
                badgeFont.setBold(true);
                p.setFont(badgeFont);
                p.setPen(Qt::white);
                p.drawText(badgeRect, Qt::AlignCenter, mentionText);
                p.restore();
            } else if (isUnread) {
                int dotSize = 6;
                int dotX = tr.tab.right() - dotSize - 3;
                int dotY = tr.tab.top() + (tr.tab.height() - dotSize) / 2;
                p.setPen(Qt::NoPen);
                p.setBrush(highlight);
                p.drawEllipse(dotX, dotY, dotSize, dotSize);
            }
        }

        // text, badge and unread dot only for normal tabs
        if (!pinned) {
            auto readIt = channelReadStates.constFind(entry.channelId);
            bool isUnread = !isActive && readIt != channelReadStates.constEnd() && readIt->unread;
            int mentions = (readIt != channelReadStates.constEnd()) ? readIt->mentionCount : 0;

            int badgeW = 0;
            QString badgeText;
            if (mentions > 0) {
                badgeText = QString::number(mentions);
                int badgeTextW = fm.horizontalAdvance(badgeText);
                badgeW = qMax(BadgeMinWidth, badgeTextW + BadgePadding * 2);
            }

            QString label = entry.name;
            if (label.isEmpty())
                label = QStringLiteral("(no channel)");

            int textLeft = iconX + IconSize + IconSpacing;
            int textRight = tr.tab.right() - TabPadding;
            if (tabs.size() > 1)
                textRight = tr.closeBtn.left() - 4;
            if (badgeW > 0)
                textRight -= badgeW + 4;

            int availW = textRight - textLeft;
            QString elidedLabel = fm.elidedText(label, Qt::ElideRight, availW);

            QColor labelColor = isActive ? textColor : (isUnread ? highlight : dimText);
            if (isUnread && !isActive) {
                p.save();
                QFont boldFont = font();
                boldFont.setBold(true);
                p.setFont(boldFont);
                p.setPen(labelColor);
                QFontMetrics bfm(boldFont);
                QString boldElided = bfm.elidedText(label, Qt::ElideRight, availW);
                p.drawText(QRect(textLeft, tr.tab.top(), availW, tr.tab.height()),
                           Qt::AlignVCenter | Qt::AlignLeft, boldElided);
                p.restore();
            } else {
                p.setPen(labelColor);
                p.drawText(QRect(textLeft, tr.tab.top(), availW, tr.tab.height()),
                           Qt::AlignVCenter | Qt::AlignLeft, elidedLabel);
            }

            if (badgeW > 0) {
                int badgeX = textRight + 4;
                int badgeY = tr.tab.top() + (tr.tab.height() - BadgeHeight) / 2;
                QRect badgeRect(badgeX, badgeY, badgeW, BadgeHeight);

                p.setPen(Qt::NoPen);
                p.setBrush(QColor(0xED, 0x42, 0x45)); // Discord red
                p.drawRoundedRect(badgeRect, BadgeHeight / 2, BadgeHeight / 2);

                p.save();
                QFont badgeFont = font();
                badgeFont.setPixelSize(10);
                badgeFont.setBold(true);
                p.setFont(badgeFont);
                p.setPen(Qt::white);
                p.drawText(badgeRect, Qt::AlignCenter, badgeText);
                p.restore();
            }

            if (isUnread && mentions == 0) {
                int dotSize = 4;
                int dotX = tr.tab.left() + 3;
                int dotY = tr.tab.top() + (tr.tab.height() - dotSize) / 2;
                p.setPen(Qt::NoPen);
                p.setBrush(highlight);
                p.drawEllipse(dotX, dotY, dotSize, dotSize);
            }

            if (tabs.size() > 1 && (isActive || isHovered)) {
                bool closeHovered = (i == hoveredClose);
                QRect cr = tr.closeBtn;

                if (closeHovered) {
                    p.setPen(Qt::NoPen);
                    QColor closeBg = altBase.lighter(140);
                    p.setBrush(closeBg);
                    p.drawRoundedRect(cr, 3, 3);
                }

                p.setPen(QPen(closeHovered ? textColor : dimText, 1.5));
                int m = 4;
                p.drawLine(cr.left() + m, cr.top() + m, cr.right() - m, cr.bottom() - m);
                p.drawLine(cr.right() - m, cr.top() + m, cr.left() + m, cr.bottom() - m);
            }
        }
    }

    // overflow button
    if (layout.overflow) {
        QRect ovRect = overflowButtonRect(true);
        QColor ovBg = hoveredOverflowButton ? blendColors(windowBg, highlight, 0.30) : windowBg;
        p.setPen(QPen(hoveredOverflowButton ? highlight : dimText, 1));
        p.setBrush(ovBg);
        p.drawRoundedRect(ovRect.adjusted(1, 1, -1, -1), 3, 3);

        p.setPen(QPen(hoveredOverflowButton ? textColor : dimText, 1.5));
        QPoint c = ovRect.center();
        int s = 4;
        p.drawLine(c.x() - s, c.y() - 1, c.x(), c.y() + 3);
        p.drawLine(c.x() + s, c.y() - 1, c.x(), c.y() + 3);
    }

    QRect addRect = addButtonRect(layout.overflow);
    QColor addBg = hoveredAddButton ? blendColors(windowBg, highlight, 0.30) : windowBg;
    p.setPen(QPen(hoveredAddButton ? highlight : dimText, 1));
    p.setBrush(addBg);
    p.drawRoundedRect(addRect, 5, 5);

    p.setPen(QPen(hoveredAddButton ? textColor : dimText, 1.6));
    QPoint center = addRect.center();
    int arm = 5;
    p.drawLine(center.x() - arm, center.y(), center.x() + arm, center.y());
    p.drawLine(center.x(), center.y() - arm, center.x(), center.y() + arm);
}

void TabBar::mousePressEvent(QMouseEvent *event)
{
    LayoutResult layout = computeTabLayout();

    if (event->button() == Qt::MiddleButton) {
        int idx = tabAtPos(event->pos());
        if (idx >= 0 && !tabs[idx].current().pinned) {
            closeTab(idx);
            return;
        }
    }

    if (event->button() == Qt::LeftButton) {
        if (overflowButtonRect(layout.overflow).contains(event->pos())) {
            showOverflowMenu();
            event->accept();
            return;
        }

        if (addButtonRect(layout.overflow).contains(event->pos())) {
            emit addTabRequested();
            event->accept();
            return;
        }

        int idx = tabAtPos(event->pos());

        // check close button first
        if (idx >= 0 && tabs.size() > 1 && !tabs[idx].current().pinned &&
            layout.rects[idx].closeBtn.contains(event->pos())) {
            closeTab(idx);
            event->accept();
            return;
        }

        if (idx >= 0) {
            dragSourceIndex = idx;
            dragStartPos = event->pos();
            dragging = false;
            switchToTab(idx);
        }
    }

    event->accept();
}

void TabBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        dragging = false;
        dragSourceIndex = -1;
    }
    event->accept();
}

void TabBar::mouseMoveEvent(QMouseEvent *event)
{
    LayoutResult layout = computeTabLayout();
    int oldHovered = hoveredTab;
    int oldHoveredClose = hoveredClose;
    bool oldHoveredAddButton = hoveredAddButton;
    bool oldHoveredOverflow = hoveredOverflowButton;

    hoveredTab = tabAtPos(event->pos());
    hoveredClose = -1;
    hoveredAddButton = addButtonRect(layout.overflow).contains(event->pos());
    hoveredOverflowButton = overflowButtonRect(layout.overflow).contains(event->pos());

    if (hoveredTab >= 0 && tabs.size() > 1 && !tabs[hoveredTab].current().pinned &&
        layout.rects[hoveredTab].closeBtn.contains(event->pos()))
        hoveredClose = hoveredTab;

    if (hoveredTab != oldHovered || hoveredClose != oldHoveredClose ||
        hoveredAddButton != oldHoveredAddButton || hoveredOverflowButton != oldHoveredOverflow)
        update();

    if (dragSourceIndex >= 0 && (event->buttons() & Qt::LeftButton)) {
        if (!dragging) {
            if ((event->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
                dragging = true;
        }

        if (dragging) {
            int targetIdx = tabAtPos(event->pos());
            if (targetIdx >= 0 && targetIdx != dragSourceIndex &&
                tabs[dragSourceIndex].current().pinned == tabs[targetIdx].current().pinned) {
                layoutDirty = true;
                tabs.move(dragSourceIndex, targetIdx);
                if (currentTabIndex == dragSourceIndex)
                    currentTabIndex = targetIdx;
                else if (dragSourceIndex < currentTabIndex && targetIdx >= currentTabIndex)
                    currentTabIndex--;
                else if (dragSourceIndex > currentTabIndex && targetIdx <= currentTabIndex)
                    currentTabIndex++;
                dragSourceIndex = targetIdx;
                update();
            }
        }
    }

    event->accept();
}

void TabBar::leaveEvent(QEvent *)
{
    if (hoveredTab != -1 || hoveredClose != -1 || hoveredAddButton || hoveredOverflowButton) {
        hoveredTab = -1;
        hoveredClose = -1;
        hoveredAddButton = false;
        hoveredOverflowButton = false;
        update();
    }
}

QSize TabBar::sizeHint() const
{
    return QSize(400, TabHeight);
}

QSize TabBar::minimumSizeHint() const
{
    return QSize(100, TabHeight);
}

} // namespace UI
} // namespace Acheron

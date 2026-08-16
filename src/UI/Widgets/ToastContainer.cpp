#include "ToastContainer.hpp"
#include "ToastNotification.hpp"

#include <QScreen>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QCursor>

namespace Acheron {
namespace UI {

using Core::Notification::NotificationPosition;

ToastContainer::ToastContainer(NotificationPosition position, QWidget *parent)
    : QWidget(parent), m_position(position)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
}

ToastContainer::~ToastContainer()
{
    qDeleteAll(m_notifications);
    m_notifications.clear();
}

void ToastContainer::addNotification(ToastNotification *notification)
{
    // Reject new notifications during a dismissAll sweep to prevent the
    // list from never emptying, which would permanently stick the flag.
    if (m_dismissingAll)
        return;

    if (m_notifications.size() >= m_maxNotifications) {
        // Remove oldest notification
        ToastNotification *oldest = m_notifications.takeFirst();
        oldest->dismiss();
    }

    m_notifications.append(notification);
    notification->setParent(this);
    notification->setScale(m_scale);
    notification->setSlideEdge(slideEdge());

    connect(notification, &ToastNotification::clicked, this, &ToastContainer::notificationClicked);
    connect(notification, &ToastNotification::iconClicked, this, &ToastContainer::notificationIconClicked);
    connect(notification, &ToastNotification::actionTriggered, this, &ToastContainer::notificationActionTriggered);
    connect(notification, &ToastNotification::replySubmitted, this, &ToastContainer::replySubmitted);
    connect(notification, &ToastNotification::groupEntryClicked, this, &ToastContainer::groupEntryClicked);
    connect(notification, &ToastNotification::dismissed, this, [this, notification]() {
        removeNotification(notification);
    });
    connect(notification, &ToastNotification::contentResized, this, [this]() {
        repositionNotifications();
    });

    // The overlay must be visible for its child toasts to render; it stays
    // hidden whenever the stack is empty.
    if (!isVisible())
        show();

    // Start off-screen beyond the toast edge so the entry animation slides in.
    const int newIndex = m_notifications.size() - 1;
    notification->move(m_animationsEnabled ? entryPositionFor(newIndex)
                                           : calculateNextPosition(newIndex));
    m_justAdded = true;

    notification->showNotification();

    // Stagger the entry slide so a burst of toasts cascades instead of
    // jumping in lockstep; existing toasts reflow immediately.
    repositionNotifications();
    if (m_animationsEnabled) {
        animateNotification(notification, calculateNextPosition(newIndex),
                            newIndex * 60, true);
    }
}

ToastNotification *ToastContainer::findByGroupKey(const QString &groupKey) const
{
    if (groupKey.isEmpty())
        return nullptr;

    for (auto *notification : m_notifications) {
        if (notification->groupKey() == groupKey)
            return notification;
    }
    return nullptr;
}

void ToastContainer::removeNotification(ToastNotification *notification)
{
    int index = m_notifications.indexOf(notification);
    if (index == -1) return;

    m_notifications.removeAt(index);
    notification->setParent(nullptr);
    repositionNotifications();

    if (m_notifications.isEmpty()) {
        if (m_dismissingAll)
            m_dismissingAll = false;
        hide();
    }
}

void ToastContainer::dismissAll()
{
    if (m_dismissingAll || m_notifications.isEmpty()) return;
    m_dismissingAll = true;

    // Call dismiss() on all notifications to start their fade-out animations.
    // Do NOT clear m_notifications: each notification's dismissed signal will
    // call removeNotification() after the animation completes, removing it
    // from the list naturally. Clearing immediately would orphan the widgets
    // and make the count check in addNotification() transiently inaccurate.
    // Iterate a copy: with animations disabled, dismiss() synchronously emits
    // dismissed -> removeNotification(), which mutates m_notifications while
    // the loop is running.
    const QList<ToastNotification *> snapshots(m_notifications);
    for (auto *notification : snapshots) {
        notification->dismiss();
    }
}

void ToastContainer::setMaxNotifications(int max)
{
    m_maxNotifications = qMax(1, max);

    // Don't trim during a dismissAll sweep — the pending fade-out animations
    // are already removing notifications naturally, and manually removing
    // them here would bypass the m_dismissingAll reset in removeNotification().
    if (!m_dismissingAll) {
        while (m_notifications.size() > m_maxNotifications) {
            ToastNotification *oldest = m_notifications.takeFirst();
            oldest->dismiss();
        }
    }

    repositionNotifications();
}

void ToastContainer::setOpacity(int opacity)
{
    m_opacity = qBound(0, opacity, 100);
    for (auto *notification : m_notifications) {
        notification->setOpacity(m_opacity / 100.0);
    }
}

void ToastContainer::setScale(qreal scale)
{
    m_scale = qBound(0.5, scale, 2.0);
    for (auto *notification : m_notifications) {
        notification->setScale(m_scale);
    }
    repositionNotifications();
}

void ToastContainer::setEdgeOffset(int offset)
{
    m_edgeOffset = qMax(0, offset);
    repositionNotifications();
}

void ToastContainer::setPosition(NotificationPosition position)
{
    if (m_position == position) return;
    m_position = position;
    repositionNotifications();
}

void ToastContainer::setAnchorWidget(QWidget *widget)
{
    if (m_anchorWidget == widget)
        return;
    m_anchorWidget = widget;

    if (widget) {
        setParent(widget);
        setWindowFlags(Qt::Widget);
    } else {
        setParent(nullptr);
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    }
    show();
    repositionNotifications();
}

void ToastContainer::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    repositionNotifications();
}

void ToastContainer::repositionNotifications()
{
    QRect geometry;
    if (m_anchorWidget) {
        geometry = QRect(0, 0, m_anchorWidget->width(), m_anchorWidget->height());
    } else {
        QScreen *screen = currentScreen();
        if (!screen)
            return;
        geometry = screen->availableGeometry();
    }

    if (QWidget::geometry() != geometry)
        setGeometry(geometry);

    const int count = m_notifications.size();
    if (count == 0) {
        m_justAdded = false;
        return;
    }

    // Prefix heights: prefix[i] is the stacked offset of the i-th toast from
    // the anchor edge, built in one linear pass so the layout loop is O(n).
    QVector<int> prefix(count, 0);
    for (int i = 1; i < count; ++i) {
        prefix[i] = prefix[i - 1] + notificationHeight(i - 1) + m_spacing;
    }
    const int totalStack = prefix[count - 1] + notificationHeight(count - 1) + m_spacing;

    QPoint start = calculateStartPosition();

    for (int i = 0; i < count; ++i) {
        // A fresh toast owns its entry animation (stagger + off-screen start);
        // reflowing it here would fight that animation.
        if (m_animationsEnabled && i == count - 1 && m_justAdded)
            continue;

        int x = start.x();
        int y = start.y();
        switch (m_position) {
        case NotificationPosition::TopLeft:
        case NotificationPosition::TopRight:
            y = start.y() + prefix[i];
            break;
        case NotificationPosition::BottomLeft:
        case NotificationPosition::BottomRight:
            y = start.y() - prefix[i] - notificationHeight(i);
            break;
        case NotificationPosition::Center:
            y = start.y() + prefix[i] - totalStack / 2;
            break;
        }
        animateNotification(m_notifications[i], QPoint(x, y));
    }
    m_justAdded = false;
}

void ToastContainer::animateNotification(ToastNotification *notification, const QPoint &targetPos,
                                         int delayMs, bool isEntry)
{
    if (!notification) return;

    if (!m_animationsEnabled) {
        notification->move(targetPos);
        return;
    }

    auto *anim = new QPropertyAnimation(notification, "pos", notification);
    anim->setDuration(isEntry ? 300 : 250);
    anim->setStartValue(notification->pos());
    anim->setEndValue(targetPos);
    // Entries get a gentle overshoot settle; reflows stay smooth and plain.
    QEasingCurve curve(isEntry ? QEasingCurve::OutBack : QEasingCurve::OutCubic);
    if (isEntry)
        curve.setOvershoot(1.2);
    anim->setEasingCurve(curve);

    if (delayMs > 0) {
        // Context-bound: if the toast is dismissed before its stagger slot,
        // the pending animation is dropped with it.
        QTimer::singleShot(delayMs, notification, [anim]() {
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        });
        return;
    }
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QPoint ToastContainer::entryPositionFor(int index) const
{
    QPoint target = calculateNextPosition(index);
    const int width = qRound(ToastNotification::defaultWidth() * m_scale);

    switch (slideEdge()) {
    case Qt::LeftEdge:
        return target + QPoint(-(width + m_edgeOffset + 40), 0);
    case Qt::RightEdge:
        return target + QPoint(width + m_edgeOffset + 40, 0);
    case Qt::TopEdge:
        return target + QPoint(0, -(notificationHeight(index) + m_edgeOffset + 40));
    default:
        return target + QPoint(0, notificationHeight(index) + m_edgeOffset + 40);
    }
}

Qt::Edge ToastContainer::slideEdge() const
{
    switch (m_position) {
    case NotificationPosition::TopLeft:
    case NotificationPosition::BottomLeft:
        return Qt::LeftEdge;
    case NotificationPosition::TopRight:
    case NotificationPosition::BottomRight:
        return Qt::RightEdge;
    case NotificationPosition::Center:
        return Qt::TopEdge;
    }
    return Qt::LeftEdge;
}

QScreen *ToastContainer::currentScreen() const
{
    if (m_cachedScreen && QGuiApplication::screens().contains(m_cachedScreen))
        return m_cachedScreen;

    if (QScreen *s = screen()) {
        m_cachedScreen = s;
        return s;
    }
    if (QScreen *s = QGuiApplication::screenAt(QCursor::pos())) {
        m_cachedScreen = s;
        return s;
    }

    m_cachedScreen = QGuiApplication::primaryScreen();
    return m_cachedScreen;
}

QPoint ToastContainer::calculateStartPosition() const
{
    QRect geometry;
    if (m_anchorWidget) {
        geometry = QRect(0, 0, m_anchorWidget->width(), m_anchorWidget->height());
    } else {
        QScreen *screen = currentScreen();
        if (!screen)
            return QPoint();
        geometry = screen->availableGeometry();
    }

    const int width = qRound(ToastNotification::defaultWidth() * m_scale);

    // Children are positioned in container-local coordinates; the container
    // itself covers the screen's available geometry.
    const int originX = geometry.left();
    const int originY = geometry.top();

    int x = 0, y = 0;

    switch (m_position) {
    case NotificationPosition::TopLeft:
        x = geometry.left() + m_edgeOffset;
        y = geometry.top() + m_edgeOffset;
        break;
    case NotificationPosition::TopRight:
        x = geometry.right() - width - m_edgeOffset;
        y = geometry.top() + m_edgeOffset;
        break;
    case NotificationPosition::BottomLeft:
        x = geometry.left() + m_edgeOffset;
        y = geometry.bottom() - m_edgeOffset;
        break;
    case NotificationPosition::BottomRight:
        x = geometry.right() - width - m_edgeOffset;
        y = geometry.bottom() - m_edgeOffset;
        break;
    case NotificationPosition::Center:
        x = geometry.center().x() - width / 2;
        y = geometry.center().y();
        break;
    }

    return QPoint(x - originX, y - originY);
}

int ToastContainer::notificationHeight(int index) const
{
    if (index < 0 || index >= m_notifications.size())
        return ToastNotification::defaultSize().height() * m_scale;

    const int height = m_notifications[index]->height();
    if (height > 0)
        return height;
    return qRound(ToastNotification::defaultSize().height() * m_scale);
}

QPoint ToastContainer::calculateNextPosition(int index) const
{
    // Stack from the anchor edge outward using each toast's real height, so
    // toasts with thumbnails or action rows don't overlap their neighbors.
    QPoint start = calculateStartPosition();

    int x = start.x();
    int y = start.y();

    switch (m_position) {
    case NotificationPosition::TopLeft:
    case NotificationPosition::TopRight:
        for (int i = 0; i < index; ++i)
            y += notificationHeight(i) + m_spacing;
        break;
    case NotificationPosition::BottomLeft:
    case NotificationPosition::BottomRight:
        y -= notificationHeight(index);
        for (int i = 0; i < index; ++i)
            y -= notificationHeight(i) + m_spacing;
        break;
    case NotificationPosition::Center:
        for (int i = 0; i < index; ++i)
            y += notificationHeight(i) + m_spacing;
        {
            int total = 0;
            for (int i = 0; i < m_notifications.size(); ++i)
                total += notificationHeight(i) + m_spacing;
            y -= total / 2;
        }
        break;
    }

    return QPoint(x, y);
}

} // namespace UI
} // namespace Acheron

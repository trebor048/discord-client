#include "ToastContainer.hpp"
#include "ToastNotification.hpp"

#include <QScreen>
#include <QGuiApplication>
#include <QPropertyAnimation>
#include <QEasingCurve>

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
    notification->setOpacity(m_opacity / 100.0);
    notification->move(calculateStartPosition());
    notification->show();

    connect(notification, &ToastNotification::clicked, this, &ToastContainer::notificationClicked);
    connect(notification, &ToastNotification::iconClicked, this, &ToastContainer::notificationIconClicked);
    connect(notification, &ToastNotification::dismissed, this, [this, notification]() {
        removeNotification(notification);
    });

    repositionNotifications();
}

void ToastContainer::removeNotification(ToastNotification *notification)
{
    int index = m_notifications.indexOf(notification);
    if (index == -1) return;

    m_notifications.removeAt(index);
    notification->setParent(nullptr);
    repositionNotifications();

    if (m_notifications.isEmpty() && m_dismissingAll) {
        m_dismissingAll = false;
    }
}

void ToastContainer::dismissAll()
{
    if (m_dismissingAll) return;
    m_dismissingAll = true;

    // Call dismiss() on all notifications to start their fade-out animations.
    // Do NOT clear m_notifications: each notification's dismissed signal will
    // call removeNotification() after the animation completes, removing it
    // from the list naturally. Clearing immediately would orphan the widgets
    // and make the count check in addNotification() transiently inaccurate.
    for (auto *notification : m_notifications) {
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

void ToastContainer::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    repositionNotifications();
}

void ToastContainer::repositionNotifications()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;

    QRect geometry = screen->availableGeometry();
    setGeometry(geometry);

    for (int i = 0; i < m_notifications.size(); ++i) {
        QPoint targetPos = calculateNextPosition(i);
        animateNotification(m_notifications[i], targetPos);
    }
}

void ToastContainer::animateNotification(ToastNotification *notification, const QPoint &targetPos)
{
    if (!notification) return;

    QPropertyAnimation *anim = new QPropertyAnimation(notification, "pos", this);
    anim->setDuration(250);
    anim->setStartValue(notification->pos());
    anim->setEndValue(targetPos);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

QPoint ToastContainer::calculateStartPosition() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return QPoint();

    QRect geometry = screen->availableGeometry();
    QSize notifSize = ToastNotification::defaultSize() * m_scale;

    int x = 0, y = 0;

    switch (m_position) {
    case NotificationPosition::TopLeft:
        x = geometry.left() + m_edgeOffset;
        y = geometry.top() + m_edgeOffset;
        break;
    case NotificationPosition::TopRight:
        x = geometry.right() - notifSize.width() - m_edgeOffset;
        y = geometry.top() + m_edgeOffset;
        break;
    case NotificationPosition::BottomLeft:
        x = geometry.left() + m_edgeOffset;
        y = geometry.bottom() - notifSize.height() - m_edgeOffset;
        break;
    case NotificationPosition::BottomRight:
        x = geometry.right() - notifSize.width() - m_edgeOffset;
        y = geometry.bottom() - notifSize.height() - m_edgeOffset;
        break;
    case NotificationPosition::Center:
        x = geometry.center().x() - notifSize.width() / 2;
        y = geometry.center().y() - notifSize.height() / 2;
        break;
    }

    return QPoint(x, y);
}

QPoint ToastContainer::calculateNextPosition(int index) const
{
    QPoint start = calculateStartPosition();
    QSize notifSize = ToastNotification::defaultSize() * m_scale;

    int x = start.x();
    int y = start.y();

    switch (m_position) {
    case NotificationPosition::TopLeft:
    case NotificationPosition::TopRight:
        y += index * (notifSize.height() + m_spacing);
        break;
    case NotificationPosition::BottomLeft:
    case NotificationPosition::BottomRight:
        y -= index * (notifSize.height() + m_spacing);
        break;
    case NotificationPosition::Center:
        y += (index - m_notifications.size() / 2) * (notifSize.height() + m_spacing);
        break;
    }

    return QPoint(x, y);
}

} // namespace UI
} // namespace Acheron

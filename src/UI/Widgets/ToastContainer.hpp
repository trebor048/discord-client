#pragma once

#include <QWidget>
#include <QList>
#include <QPropertyAnimation>
#include <QTimer>

#include "Core/Notification/NotificationTypes.hpp"

namespace Acheron {
namespace UI {

class ToastNotification;

class ToastContainer : public QWidget
{
    Q_OBJECT
public:
    explicit ToastContainer(Core::Notification::NotificationPosition position, QWidget *parent = nullptr);
    ~ToastContainer() override;

    void addNotification(ToastNotification *notification);
    void removeNotification(ToastNotification *notification);
    void dismissAll();
    void setMaxNotifications(int max);
    void setOpacity(int opacity);
    void setScale(qreal scale);
    void setEdgeOffset(int offset);
    void setPosition(Core::Notification::NotificationPosition position);

    int notificationCount() const { return m_notifications.size(); }

signals:
    void notificationClicked(const Core::Notification::ToastNotificationData &data);
    void notificationIconClicked(const Core::Notification::ToastNotificationData &data);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void repositionNotifications();
    void animateNotification(ToastNotification *notification, const QPoint &targetPos);
    QPoint calculateStartPosition() const;
    QPoint calculateNextPosition(int index) const;

    Core::Notification::NotificationPosition m_position;
    QList<ToastNotification *> m_notifications;
    int m_maxNotifications = 3;
    int m_opacity = 95;
    qreal m_scale = 1.0;
    int m_edgeOffset = 20;
    int m_spacing = 8;
    bool m_dismissingAll = false;
};

} // namespace UI
} // namespace Acheron

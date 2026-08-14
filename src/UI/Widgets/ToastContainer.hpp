#pragma once

#include <QWidget>
#include <QList>
#include <QPropertyAnimation>
#include <QTimer>
#include <QPointer>

#include "Core/Notification/NotificationTypes.hpp"

class QScreen;

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
    ToastNotification *findByGroupKey(const QString &groupKey) const;
    void dismissAll();
    void setMaxNotifications(int max);
    void setOpacity(int opacity);
    void setScale(qreal scale);
    void setEdgeOffset(int offset);
    void setPosition(Core::Notification::NotificationPosition position);
    void setAnimationsEnabled(bool enabled) { m_animationsEnabled = enabled; }

    int notificationCount() const { return m_notifications.size(); }

signals:
    void notificationClicked(const Core::Notification::ToastNotificationData &data);
    void notificationIconClicked(const Core::Notification::ToastNotificationData &data);
    void notificationActionTriggered(const QString &actionId, const Core::Notification::ToastNotificationData &data);
    void replySubmitted(ToastNotification *notification,
                        const Core::Notification::ToastNotificationData &data,
                        const QString &text);
    void groupEntryClicked(Core::Snowflake channelId, Core::Snowflake messageId);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void repositionNotifications();
    void animateNotification(ToastNotification *notification, const QPoint &targetPos,
                             int delayMs = 0, bool isEntry = false);
    QPoint calculateStartPosition() const;
    QPoint calculateNextPosition(int index) const;
    QPoint entryPositionFor(int index) const;
    int notificationHeight(int index) const;
    Qt::Edge slideEdge() const;
    QScreen *currentScreen() const;

    Core::Notification::NotificationPosition m_position;
    QList<ToastNotification *> m_notifications;
    int m_maxNotifications = 3;
    int m_opacity = 95;
    qreal m_scale = 1.0;
    int m_edgeOffset = 20;
    int m_spacing = 8;
    bool m_dismissingAll = false;
    bool m_animationsEnabled = true;
    bool m_justAdded = false;

    mutable QPointer<QScreen> m_cachedScreen;
};

} // namespace UI
} // namespace Acheron

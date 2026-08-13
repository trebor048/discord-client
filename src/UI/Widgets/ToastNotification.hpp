#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QStyleOption>
#include <functional>

#include "Core/Notification/NotificationTypes.hpp"

namespace Acheron {
namespace UI {

class ToastNotification : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
    explicit ToastNotification(const Core::Notification::ToastNotificationData &data, QWidget *parent = nullptr);
    ~ToastNotification() override;

    void showNotification();
    void dismiss();

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    void setScale(qreal scale);

    static int defaultWidth();
    static QSize defaultSize();

signals:
    void opacityChanged(qreal opacity);
    void dismissed(ToastNotification *notification);
    void clicked(const Core::Notification::ToastNotificationData &data);
    void iconClicked(const Core::Notification::ToastNotificationData &data);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void setupUi();
    void startTimeout();
    void stopTimeout();
    void animateIn();
    void animateOut();

    Core::Notification::ToastNotificationData m_data;
    qreal m_opacity = 0.0;
    bool m_hovered = false;
    bool m_dismissing = false;

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_bodyLabel = nullptr;
    QLabel *m_badgeLabel = nullptr;
    QPushButton *m_dismissButton = nullptr;

    QPropertyAnimation *m_fadeAnimation = nullptr;
    QTimer *m_timeoutTimer = nullptr;

    QColor m_badgeColor;
};

} // namespace UI
} // namespace Acheron

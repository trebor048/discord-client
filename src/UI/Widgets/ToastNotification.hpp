#pragma once

#include <QWidget>
#include <QPropertyAnimation>
#include <QTimer>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <functional>

#include "Core/Notification/NotificationTypes.hpp"

namespace Acheron {
namespace Core {
class ImageManager;
}
namespace UI {

class ToastNotification : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
    explicit ToastNotification(const Core::Notification::ToastNotificationData &data,
                               Core::ImageManager *imageManager = nullptr,
                               QWidget *parent = nullptr);
    ~ToastNotification() override;

    void showNotification();
    void dismiss();

    qreal opacity() const { return m_opacity; }
    void setOpacity(qreal opacity);

    void setScale(qreal scale);

    // Direction the toast slides in from / out toward; set by the container
    // from the configured screen position.
    void setSlideEdge(Qt::Edge edge) { m_slideEdge = edge; }

    // Grouping support: identify and update an existing toast in place.
    QString groupKey() const { return m_data.groupKey; }
    void mergeData(const Core::Notification::ToastNotificationData &data);

    bool isDismissing() const { return m_dismissing; }

    // Inline reply lifecycle, driven by NotificationManager.
    enum class ReplyState { Idle, Sending, Sent, Failed };
    void setReplyState(ReplyState state);

    static int defaultWidth();
    static QSize defaultSize();

signals:
    void opacityChanged(qreal opacity);
    void dismissed(ToastNotification *notification);
    void contentResized(ToastNotification *notification);
    void clicked(const Core::Notification::ToastNotificationData &data);
    void iconClicked(const Core::Notification::ToastNotificationData &data);
    void actionTriggered(const QString &actionId, const Core::Notification::ToastNotificationData &data);
    void replySubmitted(ToastNotification *notification,
                        const Core::Notification::ToastNotificationData &data,
                        const QString &text);
    void groupEntryClicked(Core::Snowflake channelId, Core::Snowflake messageId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // One clickable row inside an expanded group toast.
    struct GroupEntry {
        QString author;
        QString text;
        Core::Snowflake channelId;
        Core::Snowflake messageId;
    };

    void setupUi();
    void rebuildActions();
    void rebuildGroupEntries();
    void loadImages();
    void applyTheme();
    void startTimeout();
    void stopTimeout();
    void pauseCountdown();
    void resumeCountdown();
    void animateIn();
    void animateOut();
    void openReplyComposer();
    void closeReplyComposer();
    void submitReply();
    void setGroupExpanded(bool expanded);
    void updateCountLabel();
    QRect progressBarRect() const;

    Core::Notification::ToastNotificationData m_data;
    Core::ImageManager *m_imageManager = nullptr;
    qreal m_opacity = 0.0;
    bool m_hovered = false;
    bool m_dismissing = false;
    Qt::Edge m_slideEdge = Qt::LeftEdge;

    QLabel *m_iconLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_bodyLabel = nullptr;
    QPushButton *m_countButton = nullptr;
    QLabel *m_thumbnailLabel = nullptr;
    QLabel *m_badgeLabel = nullptr;
    QPushButton *m_dismissButton = nullptr;
    QVBoxLayout *m_contentLayout = nullptr;
    QHBoxLayout *m_actionsLayout = nullptr;

    // Inline reply composer
    QWidget *m_replyRow = nullptr;
    QLineEdit *m_replyEdit = nullptr;
    QLabel *m_replyStatus = nullptr;
    ReplyState m_replyState = ReplyState::Idle;

    // Expandable group list
    QWidget *m_groupPanel = nullptr;
    QVBoxLayout *m_groupLayout = nullptr;
    QList<GroupEntry> m_groupEntries;
    bool m_groupExpanded = false;
    static constexpr int MaxGroupEntries = 10;

    QPropertyAnimation *m_fadeAnimation = nullptr;
    QTimer *m_countdownTimer = nullptr;
    QTimer *m_resizeDebouncer = nullptr;
    QElapsedTimer m_countdownElapsed;
    int m_remainingMs = 0;
    qreal m_progress = 1.0;

    QColor m_avatarColor;
    QColor m_channelColor;
    QColor m_bgColor;
    QColor m_borderColor;
    QColor m_titleColor;
    QColor m_bodyColor;
    QColor m_mutedColor;
    QColor m_highlightColor;
};

} // namespace UI
} // namespace Acheron

#include "ToastNotification.hpp"

#include <QApplication>
#include <QScreen>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsOpacityEffect>
#include <QStyleOption>
#include <QPixmap>
#include <QImage>
#include <QBuffer>

namespace Acheron {
namespace UI {

ToastNotification::ToastNotification(const Core::Notification::ToastNotificationData &data, QWidget *parent)
    : QWidget(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint)
    , m_data(data)
    , m_badgeColor(data.badgeColor.isValid() ? data.badgeColor : QColor("#5865F2"))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);

    setupUi();
}

ToastNotification::~ToastNotification()
{
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
        m_fadeAnimation->deleteLater();
    }
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
        m_timeoutTimer->deleteLater();
    }
}

void ToastNotification::setupUi()
{
    setFixedWidth(defaultWidth());

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12, 10, 12, 10);
    mainLayout->setSpacing(10);

    // Icon/Avatar
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(40, 40);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->setStyleSheet("border-radius: 20px; background-color: #2f3136;");
    m_iconLabel->setCursor(Qt::PointingHandCursor);
    mainLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

    // Content area
    auto *contentLayout = new QVBoxLayout();
    contentLayout->setSpacing(2);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Title row (with badge)
    auto *titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(8);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(m_data.title, this);
    m_titleLabel->setStyleSheet("font-weight: 600; font-size: 14px; color: #dcddde;");
    m_titleLabel->setWordWrap(true);
    titleLayout->addWidget(m_titleLabel, 1);

    // Badge (colored dot)
    m_badgeLabel = new QLabel(this);
    m_badgeLabel->setFixedSize(8, 8);
    m_badgeLabel->setStyleSheet(QString("background-color: %1; border-radius: 4px;").arg(m_badgeColor.name()));
    titleLayout->addWidget(m_badgeLabel, 0, Qt::AlignTop);

    contentLayout->addLayout(titleLayout);

    // Body
    if (!m_data.body.isEmpty()) {
        m_bodyLabel = new QLabel(m_data.body, this);
        m_bodyLabel->setStyleSheet("font-size: 13px; color: #b9bbbe;");
        m_bodyLabel->setWordWrap(true);
        m_bodyLabel->setTextFormat(Qt::RichText);
        m_bodyLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_bodyLabel->setOpenExternalLinks(true);
        contentLayout->addWidget(m_bodyLabel);
    }

    // Dismiss button
    m_dismissButton = new QPushButton("×", this);
    m_dismissButton->setFixedSize(20, 20);
    m_dismissButton->setCursor(Qt::PointingHandCursor);
    m_dismissButton->setStyleSheet(
        "QPushButton {"
        "   border: none;"
        "   border-radius: 10px;"
        "   background-color: transparent;"
        "   color: #72767d;"
        "   font-size: 16px;"
        "   font-weight: bold;"
        "}"
        "QPushButton:hover {"
        "   background-color: #36393f;"
        "   color: #dcddde;"
        "}"
    );
    m_dismissButton->hide();
    connect(m_dismissButton, &QPushButton::clicked, this, &ToastNotification::dismiss);

    auto *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(m_dismissButton, 0, Qt::AlignTop);
    rightLayout->addStretch();
    contentLayout->addLayout(rightLayout);

    mainLayout->addLayout(contentLayout, 1);

    // Apply opacity effect for animation
    auto *opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.0);
    setGraphicsEffect(opacityEffect);

    // Connect title/icon clicks
    connect(m_titleLabel, &QLabel::linkActivated, this, [this](const QString &) {
        emit clicked(m_data);
    });

    if (m_bodyLabel) {
        connect(m_bodyLabel, &QLabel::linkActivated, this, [this](const QString &) {
            emit clicked(m_data);
        });
    }
}

int ToastNotification::defaultWidth()
{
    return 340;
}

QSize ToastNotification::defaultSize()
{
    return QSize(defaultWidth(), 80);
}

void ToastNotification::setScale(qreal scale)
{
    scale = qBound(0.5, scale, 2.0);
    resize(defaultSize() * scale);
}

void ToastNotification::showNotification()
{
    show();
    animateIn();
    startTimeout();
}

void ToastNotification::dismiss()
{
    if (m_dismissing) return;
    m_dismissing = true;
    stopTimeout();
    animateOut();
}

void ToastNotification::setOpacity(qreal opacity)
{
    m_opacity = opacity;
    if (auto *effect = qobject_cast<QGraphicsOpacityEffect *>(graphicsEffect())) {
        effect->setOpacity(opacity);
    }
    emit opacityChanged(opacity);
}

void ToastNotification::startTimeout()
{
    if (m_data.timeout <= 0) return;

    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer, &QTimer::timeout, this, &ToastNotification::dismiss);
    m_timeoutTimer->start(m_data.timeout * 1000);
}

void ToastNotification::stopTimeout()
{
    if (m_timeoutTimer) {
        m_timeoutTimer->stop();
    }
}

void ToastNotification::animateIn()
{
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
        m_fadeAnimation->deleteLater();
    }

    m_fadeAnimation = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnimation->setDuration(200);
    m_fadeAnimation->setStartValue(0.0);
    m_fadeAnimation->setEndValue(m_data.opacity / 100.0);
    m_fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastNotification::animateOut()
{
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
        m_fadeAnimation->deleteLater();
    }

    m_fadeAnimation = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnimation->setDuration(200);
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InCubic);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        emit dismissed(this);
        close();
        deleteLater();
    });
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastNotification::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}

void ToastNotification::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_data);
    }
    QWidget::mousePressEvent(event);
}

void ToastNotification::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    m_dismissButton->show();
    stopTimeout();
    QWidget::enterEvent(event);
}

void ToastNotification::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    m_dismissButton->hide();
    if (!m_dismissing) {
        startTimeout();
    }
    QWidget::leaveEvent(event);
}

} // namespace UI
} // namespace Acheron

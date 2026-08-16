#include "ToastNotification.hpp"

#include <QApplication>
#include <QScreen>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QStyleOption>
#include <QPixmap>
#include <QImage>
#include <QBuffer>
#include <QKeyEvent>

#include <optional>

#include "Core/ImageManager.hpp"
#include "Core/Theme/Manager.hpp"

namespace Acheron {
namespace UI {

// Painted-shadow margins: the widget is transparent, the card is inset so a
// soft shadow can be drawn around/below it.
static constexpr int ShadowMarginX = 8;
static constexpr int ShadowMarginTop = 5;
static constexpr int ShadowMarginBottom = 11;
static constexpr int CountdownTickMs = 100;

static std::optional<Core::Snowflake> snowflakeFromString(const QString &str)
{
    bool ok = false;
    const quint64 value = str.toULongLong(&ok);
    if (!ok || value == 0)
        return std::nullopt;

    Core::Snowflake id(value);
    if (!id.isValid())
        return std::nullopt;

    return id;
}

ToastNotification::ToastNotification(const Core::Notification::ToastNotificationData &data,
                                     Core::ImageManager *imageManager, QWidget *parent)
    : QWidget(parent)
    , m_data(data)
    , m_imageManager(imageManager)
    , m_avatarColor(data.coloredAccents && data.badgeColor.isValid()
                            ? data.badgeColor
                            : Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight))
    , m_channelColor(data.coloredAccents && data.channelColor.isValid()
                             ? data.channelColor
                             : Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight))
{
    setAttribute(Qt::WA_StyledBackground, false);

    // Render each toast as a child window so we can fade with the compositor
    // via setWindowOpacity() instead of the expensive QGraphicsOpacityEffect,
    // which forces full subtree repaints every animation frame.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);

    applyTheme();
    setupUi();

    connect(&Core::Theme::Manager::instance(), &Core::Theme::Manager::themeChanged,
            this, [this]() {
        applyTheme();
        update();
    });
}

ToastNotification::~ToastNotification() = default;

void ToastNotification::applyTheme()
{
    auto &theme = Core::Theme::Manager::instance();
    m_bgColor = theme.color(Core::Theme::Token::BaseBg);
    m_borderColor = theme.color(Core::Theme::Token::Divider);
    m_titleColor = theme.color(Core::Theme::Token::PrimaryText);
    m_bodyColor = theme.color(Core::Theme::Token::WindowText);
    m_mutedColor = theme.color(Core::Theme::Token::PlaceholderText);
    m_highlightColor = theme.color(Core::Theme::Token::Highlight);

    if (m_data.coloredAccents) {
        m_avatarColor = m_data.badgeColor.isValid() ? m_data.badgeColor : m_highlightColor;
        m_channelColor = m_data.channelColor.isValid() ? m_data.channelColor : m_highlightColor;
    } else {
        m_avatarColor = m_highlightColor;
        m_channelColor = m_highlightColor;
    }
}

void ToastNotification::setupUi()
{
    setFixedWidth(defaultWidth());

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(12 + ShadowMarginX, 10 + ShadowMarginTop,
                                   12 + ShadowMarginX, 10 + ShadowMarginBottom);
    mainLayout->setSpacing(10);

    // Icon/Avatar
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(40, 40);
    m_iconLabel->setScaledContents(true);
    m_iconLabel->setStyleSheet(QStringLiteral("border-radius: 20px; background-color: %1;")
                                       .arg(m_borderColor.name()));
    m_iconLabel->setCursor(Qt::PointingHandCursor);
    m_iconLabel->installEventFilter(this);
    mainLayout->addWidget(m_iconLabel, 0, Qt::AlignTop);

    // Content area
    m_contentLayout = new QVBoxLayout();
    m_contentLayout->setSpacing(2);
    m_contentLayout->setContentsMargins(0, 0, 0, 0);

    // Title row (with badge)
    auto *titleLayout = new QHBoxLayout();
    titleLayout->setSpacing(8);
    titleLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(m_data.title, this);
    m_titleLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 14px; color: %1; background: transparent;")
                                        .arg(m_titleColor.name()));
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setTextFormat(Qt::PlainText);
    titleLayout->addWidget(m_titleLabel, 1);

    // Channel-color chip (colored dot)
    m_badgeLabel = new QLabel(this);
    m_badgeLabel->setFixedSize(8, 8);
    m_badgeLabel->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;")
                                        .arg(m_channelColor.name()));
    titleLayout->addWidget(m_badgeLabel, 0, Qt::AlignTop);

    m_contentLayout->addLayout(titleLayout);

    // Grouped-message counter / expand-collapse affordance
    m_countButton = new QPushButton(this);
    m_countButton->setCursor(Qt::PointingHandCursor);
    m_countButton->setFlat(true);
    m_countButton->setStyleSheet(QStringLiteral(
            "QPushButton { text-align: left; border: none; background: transparent;"
            "  font-size: 11px; color: %1; padding: 0px; }"
            "QPushButton:hover { color: %2; }")
            .arg(m_mutedColor.name(), m_highlightColor.name()));
    m_countButton->hide();
    connect(m_countButton, &QPushButton::clicked, this, [this]() {
        setGroupExpanded(!m_groupExpanded);
    });
    m_contentLayout->addWidget(m_countButton);

    // Expandable group panel (animated height)
    m_groupPanel = new QWidget(this);
    m_groupLayout = new QVBoxLayout(m_groupPanel);
    m_groupLayout->setContentsMargins(0, 2, 0, 2);
    m_groupLayout->setSpacing(1);
    m_groupPanel->setMaximumHeight(0);
    m_groupPanel->hide();
    m_contentLayout->addWidget(m_groupPanel);

    // Body
    if (!m_data.body.isEmpty()) {
        m_bodyLabel = new QLabel(m_data.body.toHtmlEscaped(), this);
        m_bodyLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: %1; background: transparent;")
                                           .arg(m_bodyColor.name()));
        m_bodyLabel->setWordWrap(true);
        m_bodyLabel->setTextFormat(Qt::RichText);
        m_bodyLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
        m_bodyLabel->setOpenExternalLinks(true);
        m_contentLayout->addWidget(m_bodyLabel);
    }

    // Image attachment thumbnail
    if (!m_data.thumbnailUrl.isEmpty()) {
        m_thumbnailLabel = new QLabel(this);
        m_thumbnailLabel->setFixedHeight(80);
        m_thumbnailLabel->setStyleSheet(QStringLiteral("border-radius: 6px; background-color: %1;")
                                                .arg(m_borderColor.name()));
        m_thumbnailLabel->setScaledContents(false);
        m_thumbnailLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_contentLayout->addWidget(m_thumbnailLabel);
    }

    // Quick actions (Reply, ...)
    m_actionsLayout = new QHBoxLayout();
    m_actionsLayout->setSpacing(6);
    m_actionsLayout->setContentsMargins(0, 4, 0, 0);
    m_contentLayout->addLayout(m_actionsLayout);
    rebuildActions();

    // Inline reply composer (hidden until Reply is triggered)
    m_replyRow = new QWidget(this);
    auto *replyLayout = new QHBoxLayout(m_replyRow);
    replyLayout->setContentsMargins(0, 4, 0, 0);
    replyLayout->setSpacing(6);

    m_replyEdit = new QLineEdit(m_replyRow);
    m_replyEdit->setPlaceholderText(tr("Reply to %1...").arg(m_data.authorName.isEmpty()
                                                                     ? m_data.title
                                                                     : m_data.authorName));
    m_replyEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { border: 1px solid %1; border-radius: 4px; background-color: %2;"
            "  color: %3; font-size: 12px; padding: 3px 8px; }"
            "QLineEdit:focus { border-color: %4; }")
            .arg(m_borderColor.name(), m_bgColor.darker(105).name(),
                 m_bodyColor.name(), m_highlightColor.name()));
    m_replyEdit->installEventFilter(this);
    replyLayout->addWidget(m_replyEdit, 1);

    m_replyStatus = new QLabel(m_replyRow);
    m_replyStatus->setStyleSheet(QStringLiteral("font-size: 11px; color: %1; background: transparent;")
                                         .arg(m_mutedColor.name()));
    m_replyStatus->hide();
    replyLayout->addWidget(m_replyStatus);

    m_replyRow->hide();
    m_contentLayout->addWidget(m_replyRow);

    connect(m_replyEdit, &QLineEdit::returnPressed, this, &ToastNotification::submitReply);

    mainLayout->addLayout(m_contentLayout, 1);

    // Dismiss button
    m_dismissButton = new QPushButton(QStringLiteral("×"), this);
    m_dismissButton->setFixedSize(20, 20);
    m_dismissButton->setCursor(Qt::PointingHandCursor);
    m_dismissButton->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "   border: none;"
            "   border-radius: 10px;"
            "   background-color: transparent;"
            "   color: %1;"
            "   font-size: 16px;"
            "   font-weight: bold;"
            "}"
            "QPushButton:hover {"
            "   background-color: %2;"
            "   color: %3;"
            "}")
            .arg(m_mutedColor.name(), m_borderColor.name(), m_titleColor.name()));
    m_dismissButton->hide();
    connect(m_dismissButton, &QPushButton::clicked, this, &ToastNotification::dismiss);

    auto *rightLayout = new QVBoxLayout();
    rightLayout->addWidget(m_dismissButton, 0, Qt::AlignTop);
    rightLayout->addStretch();
    mainLayout->addLayout(rightLayout);

    // Connect title/body link clicks
    connect(m_titleLabel, &QLabel::linkActivated, this, [this](const QString &) {
        emit clicked(m_data);
    });

    if (m_bodyLabel) {
        connect(m_bodyLabel, &QLabel::linkActivated, this, [this](const QString &) {
            emit clicked(m_data);
        });
    }

    // Seed the group history with the first message so an expanded group
    // always contains at least the currently displayed entry.
    if (!m_data.groupKey.isEmpty()) {
        if (auto channelId = snowflakeFromString(m_data.channelId))
            m_groupEntries.append({ m_data.authorName, m_data.body, *channelId, m_data.messageId });
    }

    loadImages();
    adjustSize();
}

void ToastNotification::rebuildActions()
{
    QLayoutItem *child = nullptr;
    while ((child = m_actionsLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    for (const auto &action : m_data.actions) {
        auto *button = new QPushButton(action.label, this);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   border: 1px solid %1;"
                "   border-radius: 4px;"
                "   background-color: %2;"
                "   color: %3;"
                "   font-size: 12px;"
                "   padding: 3px 10px;"
                "}"
                "QPushButton:hover {"
                "   background-color: %4;"
                "}")
                .arg(m_borderColor.name(), m_bgColor.lighter(110).name(),
                     m_bodyColor.name(), m_bgColor.lighter(120).name()));
        connect(button, &QPushButton::clicked, this, [this, actionId = action.id]() {
            // "reply" expands the inline composer locally; the manager only
            // learns about it when text is actually submitted.
            if (actionId == QLatin1String("reply")) {
                openReplyComposer();
                return;
            }
            emit actionTriggered(actionId, m_data);
            if (m_data.onAction)
                m_data.onAction(actionId);
        });
        m_actionsLayout->addWidget(button);
    }
    m_actionsLayout->addStretch();
}

void ToastNotification::rebuildGroupEntries()
{
    QLayoutItem *child = nullptr;
    while ((child = m_groupLayout->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }

    for (const auto &entry : m_groupEntries) {
        const QString author = entry.author.isEmpty() ? tr("Unknown") : entry.author;
        auto *row = new QPushButton(QStringLiteral("%1: %2").arg(author, entry.text), m_groupPanel);
        row->setCursor(Qt::PointingHandCursor);
        row->setFlat(true);
        row->setToolTip(entry.text);
        row->setStyleSheet(QStringLiteral(
                "QPushButton { text-align: left; border: none; border-radius: 3px;"
                "  background: transparent; font-size: 12px; color: %1; padding: 2px 4px; }"
                "QPushButton:hover { background-color: %2; color: %3; }")
                .arg(m_bodyColor.name(), m_bgColor.lighter(115).name(), m_titleColor.name()));
        // Elide long lines so the toast keeps its fixed width.
        QFontMetrics metrics(row->font());
        const int available = defaultWidth() - 90;
        row->setText(metrics.elidedText(QStringLiteral("%1: %2").arg(author, entry.text),
                                        Qt::ElideRight, available));
        connect(row, &QPushButton::clicked, this, [this, entry]() {
            if (entry.channelId.isValid())
                emit groupEntryClicked(entry.channelId, entry.messageId);
        });
        m_groupLayout->addWidget(row);
    }
}

void ToastNotification::setGroupExpanded(bool expanded)
{
    if (expanded == m_groupExpanded)
        return;
    if (expanded && m_groupEntries.size() < 2)
        return;
    m_groupExpanded = expanded;

    if (expanded) {
        rebuildGroupEntries();
        // Reading a group's contents takes time; hold the toast open.
        pauseCountdown();
        m_groupPanel->show();
    } else if (!m_hovered && !m_dismissing) {
        resumeCountdown();
    }

    updateCountLabel();

    m_groupPanel->adjustSize();
    adjustSize();
    const int targetHeight = expanded ? m_groupPanel->sizeHint().height() : 0;

    if (!m_resizeDebouncer) {
        m_resizeDebouncer = new QTimer(this);
        m_resizeDebouncer->setSingleShot(true);
        m_resizeDebouncer->setInterval(80);
        connect(m_resizeDebouncer, &QTimer::timeout, this, [this]() {
            emit contentResized(this);
        });
    }

    if (!m_data.animationsEnabled) {
        m_groupPanel->setMaximumHeight(targetHeight);
        if (!expanded)
            m_groupPanel->hide();
        adjustSize();
        m_resizeDebouncer->stop();
        emit contentResized(this);
        return;
    }

    auto *anim = new QPropertyAnimation(m_groupPanel, "maximumHeight", this);
    anim->setDuration(180);
    anim->setStartValue(m_groupPanel->maximumHeight());
    anim->setEndValue(targetHeight);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QPropertyAnimation::valueChanged, this, [this](const QVariant &) {
        adjustSize();
        m_resizeDebouncer->start();
    });
    connect(anim, &QPropertyAnimation::finished, this, [this, expanded]() {
        m_resizeDebouncer->stop();
        if (!expanded)
            m_groupPanel->hide();
        adjustSize();
        emit contentResized(this);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastNotification::updateCountLabel()
{
    if (m_data.messageCount < 2) {
        m_countButton->hide();
        return;
    }
    const auto arrow = m_groupExpanded ? QStringLiteral("▾") : QStringLiteral("▸");
    m_countButton->setText(tr("%1 %2 messages — click to %3")
                                   .arg(arrow)
                                   .arg(m_data.messageCount)
                                   .arg(m_groupExpanded ? tr("collapse") : tr("expand")));
    m_countButton->show();
}

QRect ToastNotification::progressBarRect() const
{
    const int left = ShadowMarginX + 2;
    const int right = width() - ShadowMarginX - 2;
    const int cardBottom = height() - ShadowMarginBottom;
    const int barHeight = 4;
    return QRect(left, cardBottom - barHeight - 1, qMax(0, right - left), barHeight);
}

void ToastNotification::openReplyComposer()
{
    if (m_replyState == ReplyState::Sending)
        return;

    m_replyRow->show();
    m_replyStatus->hide();
    m_replyEdit->setEnabled(true);
    m_replyEdit->setFocus();
    // Composing takes longer than the auto-dismiss countdown; hold open.
    pauseCountdown();
    adjustSize();
    emit contentResized(this);
}

void ToastNotification::closeReplyComposer()
{
    m_replyRow->hide();
    m_replyEdit->clear();
    m_replyState = ReplyState::Idle;
    if (!m_hovered && !m_dismissing)
        resumeCountdown();
    adjustSize();
    emit contentResized(this);
}

void ToastNotification::submitReply()
{
    const QString text = m_replyEdit->text().trimmed();
    if (text.isEmpty() || m_replyState == ReplyState::Sending)
        return;

    setReplyState(ReplyState::Sending);
    emit replySubmitted(this, m_data, text);
}

void ToastNotification::setReplyState(ReplyState state)
{
    m_replyState = state;

    switch (state) {
    case ReplyState::Idle:
        m_replyStatus->hide();
        m_replyEdit->setEnabled(true);
        return;
    case ReplyState::Sending:
        m_replyEdit->setEnabled(false);
        m_replyStatus->setText(tr("Sending…"));
        m_replyStatus->setStyleSheet(QStringLiteral("font-size: 11px; color: %1; background: transparent;")
                                             .arg(m_mutedColor.name()));
        m_replyStatus->show();
        return;
    case ReplyState::Sent:
        m_replyStatus->setText(tr("Sent ✓"));
        m_replyStatus->setStyleSheet(QStringLiteral("font-size: 11px; color: %1; background: transparent;")
                                             .arg(m_highlightColor.name()));
        m_replyStatus->show();
        m_replyEdit->clear();
        // Brief confirmation, then let the toast go away on its own.
        QTimer::singleShot(900, this, [this]() {
            if (m_replyState == ReplyState::Sent)
                dismiss();
        });
        return;
    case ReplyState::Failed:
        m_replyStatus->setText(tr("Failed to send"));
        m_replyStatus->setStyleSheet(QStringLiteral("font-size: 11px; color: %1; background: transparent;")
                                             .arg(Core::Theme::Manager::instance()
                                                          .color(Core::Theme::Token::ChatError)
                                                          .name()));
        m_replyStatus->show();
        m_replyEdit->setEnabled(true);
        m_replyEdit->setFocus();
        return;
    }
}

void ToastNotification::loadImages()
{
    if (!m_imageManager)
        return;

    if (!m_data.iconUrl.isEmpty()) {
        m_imageManager->assign(m_iconLabel, QUrl(m_data.iconUrl), m_iconLabel->size());
    }

    if (m_thumbnailLabel && !m_data.thumbnailUrl.isEmpty()) {
        m_imageManager->assign(m_thumbnailLabel, QUrl(m_data.thumbnailUrl),
                               QSize(defaultWidth() - 64, 80));
    }
}

void ToastNotification::mergeData(const Core::Notification::ToastNotificationData &data)
{
    // Preserve the grouping continuity: the existing toast absorbs the new
    // message, refreshing the visible body and bumping the collapsed count.
    const int count = m_data.messageCount + 1;
    const bool animationsEnabled = m_data.animationsEnabled;
    const bool showProgressBar = m_data.showProgressBar;
    const auto previousActions = m_data.actions;
    m_data = data;
    m_data.messageCount = count;
    m_data.animationsEnabled = animationsEnabled;
    m_data.showProgressBar = showProgressBar;

    if (m_data.coloredAccents) {
        m_avatarColor = m_data.badgeColor.isValid() ? m_data.badgeColor : m_highlightColor;
        m_channelColor = m_data.channelColor.isValid() ? m_data.channelColor : m_highlightColor;
    } else {
        m_avatarColor = m_highlightColor;
        m_channelColor = m_highlightColor;
    }

    m_badgeLabel->setStyleSheet(QStringLiteral("background-color: %1; border-radius: 4px;")
                                        .arg(m_channelColor.name()));

    m_titleLabel->setText(m_data.title);
    if (m_bodyLabel)
        m_bodyLabel->setText(m_data.body.toHtmlEscaped());

    // Record the message in the bounded group history for expansion.
    if (auto channelId = snowflakeFromString(m_data.channelId))
        m_groupEntries.append({ m_data.authorName, m_data.body, *channelId, m_data.messageId });
    while (m_groupEntries.size() > MaxGroupEntries)
        m_groupEntries.removeFirst();

    updateCountLabel();
    if (m_groupExpanded)
        rebuildGroupEntries();

    if (m_thumbnailLabel && !m_data.thumbnailUrl.isEmpty() && m_imageManager) {
        m_imageManager->assign(m_thumbnailLabel, QUrl(m_data.thumbnailUrl),
                               QSize(defaultWidth() - 64, 80));
    }

    if (!m_data.iconUrl.isEmpty() && m_imageManager)
        m_imageManager->assign(m_iconLabel, QUrl(m_data.iconUrl), m_iconLabel->size());

    if (previousActions != m_data.actions)
        rebuildActions();
    adjustSize();
    emit contentResized(this);

    // Reset the auto-dismiss countdown so the merged toast lingers. When the
    // timer is currently running we must also restart m_countdownElapsed,
    // otherwise the existing elapsed time is applied to the fresh timeout and
    // the toast dismisses earlier than configured.
    m_remainingMs = m_data.timeout * 1000;
    m_progress = 1.0;
    if (!m_hovered && !m_groupExpanded && m_replyRow->isHidden()) {
        stopTimeout();
        startTimeout();
    } else if (m_countdownTimer && m_countdownTimer->isActive()) {
        m_countdownElapsed.start();
        update();
    } else {
        update();
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
    setFixedWidth(qRound(defaultWidth() * scale));
    adjustSize();
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
    setWindowOpacity(opacity);
    emit opacityChanged(opacity);
}

void ToastNotification::startTimeout()
{
    if (m_data.timeout <= 0) return;

    m_remainingMs = m_data.timeout * 1000;
    m_progress = 1.0;

    if (!m_countdownTimer) {
        m_countdownTimer = new QTimer(this);
        m_countdownTimer->setInterval(CountdownTickMs);
        connect(m_countdownTimer, &QTimer::timeout, this, [this]() {
            const int remaining = m_remainingMs - int(m_countdownElapsed.elapsed());
            if (remaining <= 0) {
                stopTimeout();
                dismiss();
                return;
            }
            if (m_data.timeout > 0)
                m_progress = qreal(remaining) / qreal(m_data.timeout * 1000);
            if (m_data.showProgressBar)
                update(progressBarRect());
        });
    }
    m_countdownElapsed.start();
    m_countdownTimer->start();
}

void ToastNotification::stopTimeout()
{
    if (m_countdownTimer)
        m_countdownTimer->stop();
}

void ToastNotification::pauseCountdown()
{
    if (!m_countdownTimer || !m_countdownTimer->isActive())
        return;
    // Bank the elapsed time so resume continues where we paused.
    m_remainingMs = qMax(0, m_remainingMs - int(m_countdownElapsed.elapsed()));
    stopTimeout();
}

void ToastNotification::resumeCountdown()
{
    if (m_data.timeout <= 0 || m_remainingMs <= 0 || m_dismissing)
        return;
    if (!m_countdownTimer)
        return;
    m_countdownElapsed.start();
    m_countdownTimer->start();
}

void ToastNotification::animateIn()
{
    if (m_fadeAnimation) {
        m_fadeAnimation->stop();
        m_fadeAnimation->deleteLater();
    }

    if (!m_data.animationsEnabled) {
        setOpacity(m_data.opacity / 100.0);
        return;
    }

    m_fadeAnimation = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnimation->setDuration(220);
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

    const auto finishDismissal = [this]() {
        emit dismissed(this);
        close();
        deleteLater();
    };

    if (!m_data.animationsEnabled) {
        finishDismissal();
        return;
    }

    // Slide toward the toast's edge while fading out.
    QPoint offset;
    switch (m_slideEdge) {
    case Qt::LeftEdge: offset = QPoint(-60, 0); break;
    case Qt::RightEdge: offset = QPoint(60, 0); break;
    case Qt::TopEdge: offset = QPoint(0, -40); break;
    default: offset = QPoint(0, 40); break;
    }
    auto *slide = new QPropertyAnimation(this, "pos", this);
    slide->setDuration(200);
    slide->setStartValue(pos());
    slide->setEndValue(pos() + offset);
    slide->setEasingCurve(QEasingCurve::InCubic);
    slide->start(QAbstractAnimation::DeleteWhenStopped);

    m_fadeAnimation = new QPropertyAnimation(this, "opacity", this);
    m_fadeAnimation->setDuration(200);
    m_fadeAnimation->setStartValue(m_opacity);
    m_fadeAnimation->setEndValue(0.0);
    m_fadeAnimation->setEasingCurve(QEasingCurve::InCubic);
    connect(m_fadeAnimation, &QPropertyAnimation::finished, this, finishDismissal);
    m_fadeAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToastNotification::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF card = QRectF(rect()).adjusted(ShadowMarginX, ShadowMarginTop,
                                                -ShadowMarginX, -ShadowMarginBottom);

    const qreal r = Core::Theme::Manager::instance().roundness();

    // Soft painted drop shadow: stacked rounded rects with falloff alpha.
    const QColor shadowBase(0, 0, 0);
    for (int i = 1; i <= 3; ++i) {
        QColor layer = shadowBase;
        layer.setAlpha(14 - i * 3);
        QPainterPath shadowPath;
        shadowPath.addRoundedRect(card.translated(0, i * 2.0), r + i, r + i);
        p.fillPath(shadowPath, layer);
    }

    QColor bg = m_bgColor;
    QColor border = m_borderColor;
    if (m_hovered) {
        bg = bg.lighter(108);
        border = m_avatarColor;
        border.setAlpha(160);
    }

    QPainterPath background;
    background.addRoundedRect(card, r, r);
    p.fillPath(background, bg);
    p.setPen(QPen(border, 1));
    p.drawPath(background);

    // Accent bar in the per-author avatar color along the left edge
    QRectF accentRect = card.adjusted(0, 0, 0, 0);
    accentRect.setWidth(4);
    QPainterPath accent;
    accent.addRoundedRect(accentRect.adjusted(0, 6, 0, -6), 2.0, 2.0);
    p.fillPath(accent, m_avatarColor);

    // Countdown progress bar along the card's bottom edge
    if (m_data.showProgressBar && m_data.timeout > 0 && !m_dismissing && m_progress < 1.0) {
        const qreal width = card.width() * qBound(0.0, m_progress, 1.0);
        QRectF bar(card.left() + 2, card.bottom() - 2.5, qMax(0.0, width - 4), 2.5);
        QColor barColor = m_avatarColor;
        barColor.setAlpha(180);
        QPainterPath barPath;
        barPath.addRoundedRect(bar, 1.25, 1.25);
        p.fillPath(barPath, barColor);
    }
}

void ToastNotification::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit clicked(m_data);
    }
    QWidget::mousePressEvent(event);
}

bool ToastNotification::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_iconLabel && event->type() == QEvent::MouseButtonRelease) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            emit iconClicked(m_data);
            return true;
        }
    }
    if (watched == m_replyEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            closeReplyComposer();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ToastNotification::enterEvent(QEnterEvent *event)
{
    Q_UNUSED(event);
    m_hovered = true;
    m_dismissButton->show();
    if (m_data.pauseOnHover)
        pauseCountdown();
    update();
    QWidget::enterEvent(event);
}

void ToastNotification::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    m_hovered = false;
    m_dismissButton->hide();
    if (!m_dismissing && m_data.pauseOnHover && !m_groupExpanded && m_replyRow->isHidden()) {
        resumeCountdown();
    }
    update();
    QWidget::leaveEvent(event);
}

} // namespace UI
} // namespace Acheron

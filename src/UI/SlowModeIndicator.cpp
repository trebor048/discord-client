#include "SlowModeIndicator.hpp"
#include "Core/AnimationUtils.hpp"
#include <QHBoxLayout>
#include <QPropertyAnimation>

namespace Acheron {
namespace UI {

SlowModeIndicator::SlowModeIndicator(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 16, 0);
    layout->setSpacing(0);

    layout->addStretch();

    label = new QLabel(this);
    label->setTextFormat(Qt::PlainText);
    label->setStyleSheet("font-size: 12px; color: #b5bac1;");
    layout->addWidget(label);

    label->setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);

    countdownTimer = new QTimer(this);
    countdownTimer->setInterval(500);
    connect(countdownTimer, &QTimer::timeout, this, &SlowModeIndicator::updateDisplay);
}

void SlowModeIndicator::setSlowMode(Core::Snowflake channelId, int seconds, bool canBypass)
{
    activeChannelId = channelId;
    activeRateLimit = seconds;
    activeCanBypass = canBypass;
    updateDisplay();
}

void SlowModeIndicator::startCooldown(Core::Snowflake channelId, int seconds)
{
    cooldownEndTimes[channelId] = QDateTime::currentDateTimeUtc().addSecs(seconds);

    if (channelId == activeChannelId)
        updateDisplay();
}

bool SlowModeIndicator::isOnCooldown(Core::Snowflake channelId) const
{
    auto it = cooldownEndTimes.find(channelId);
    if (it == cooldownEndTimes.end())
        return false;
    return QDateTime::currentDateTimeUtc() < it.value();
}

void SlowModeIndicator::updateDisplay()
{
    const bool relevant = activeRateLimit > 0;
    if (relevant != activityActive_) {
        activityActive_ = relevant;
        emit activityChanged(relevant);
    }

    if (activeRateLimit <= 0) {
        if (label->isVisible())
            Core::AnimationUtils::fadeOut(label, 120);
        countdownTimer->stop();
        return;
    }

    if (activeCanBypass) {
        label->setText("Slowmode bypassed");
        if (!label->isVisible()) {
            label->setVisible(true);
            Core::AnimationUtils::fadeIn(label, 150);
        }
        countdownTimer->stop();
        return;
    }

    auto it = cooldownEndTimes.find(activeChannelId);
    if (it != cooldownEndTimes.end()) {
        QDateTime now = QDateTime::currentDateTimeUtc();
        if (now < it.value()) {
            qint64 remainingMs = now.msecsTo(it.value());
            int remaining = static_cast<int>((remainingMs + 999) / 1000);
            if (remaining <= 0)
                remaining = 1;
            label->setText(QString("Slowmode: %1s remaining").arg(remaining));
            if (!label->isVisible()) {
                label->setVisible(true);
                Core::AnimationUtils::fadeIn(label, 150);
            }
            if (!countdownTimer->isActive())
                countdownTimer->start();
            return;
        }

        // expired
        cooldownEndTimes.erase(it);
        emit cooldownChanged(false);
    }

    // not on cooldown
    label->setText(QString("Slowmode: %1s").arg(activeRateLimit));
    if (!label->isVisible()) {
        label->setVisible(true);
        Core::AnimationUtils::fadeIn(label, 150);
    }
    countdownTimer->stop();
}

} // namespace UI
} // namespace Acheron

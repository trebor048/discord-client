#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QHash>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class SlowModeIndicator : public QWidget
{
    Q_OBJECT
public:
    explicit SlowModeIndicator(QWidget *parent = nullptr);

    void setSlowMode(Core::Snowflake channelId, int seconds, bool canBypass);
    void startCooldown(Core::Snowflake channelId, int seconds);
    [[nodiscard]] bool isOnCooldown(Core::Snowflake channelId) const;

signals:
    void cooldownChanged(bool onCooldown);

private:
    void updateDisplay();

    QLabel *label;
    QTimer *countdownTimer;

    Core::Snowflake activeChannelId;
    int activeRateLimit = 0;
    bool activeCanBypass = false;

    QHash<Core::Snowflake, QDateTime> cooldownEndTimes;
};

} // namespace UI
} // namespace Acheron

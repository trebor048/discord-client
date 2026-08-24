#include "Core/Animation/AnimationConfig.hpp"

#include <QSettings>

#include <algorithm>
#include <cmath>

namespace Acheron {
namespace Core {

AnimationConfig &AnimationConfig::instance()
{
    static AnimationConfig config;
    return config;
}

AnimationConfig::AnimationConfig()
{
    load();
}

void AnimationConfig::load()
{
    QSettings settings;
    speed_ = settings.value(kSpeedKey, 1.0).toFloat();
    reduceMotion_ = settings.value(kReduceMotionKey, false).toBool();
}

void AnimationConfig::save() const
{
    QSettings settings;
    settings.setValue(kSpeedKey, speed_);
    settings.setValue(kReduceMotionKey, reduceMotion_);
    settings.sync();
}

int AnimationConfig::scaled(int baseMs) const
{
    if (reduceMotion_ || baseMs <= 0)
        return 0;
    const float s = speed_ > 0.0f ? speed_ : 1.0f;
    return std::max(1, static_cast<int>(std::lround(baseMs * s)));
}

void AnimationConfig::setSpeed(float speed)
{
    speed = std::clamp(speed, 0.25f, 4.0f);
    if (qFuzzyCompare(speed, speed_))
        return;
    speed_ = speed;
    save();
    emit speedChanged(speed_);
    emit configChanged();
}

void AnimationConfig::setReduceMotion(bool on)
{
    if (on == reduceMotion_)
        return;
    reduceMotion_ = on;
    save();
    emit reduceMotionChanged(on);
    emit configChanged();
}

} // namespace Core
} // namespace Acheron

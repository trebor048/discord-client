#pragma once

#include <QObject>

namespace Acheron {
namespace Core {

/// Global animation preferences: a speed multiplier and a reduce-motion flag,
/// persisted in QSettings under `ui/`. Every animation in the app routes its
/// duration through AnimationUtils so a single change here retunes everything.
///
/// - speed() scales every duration (1.0 = the authored baseline).
/// - reduceMotion() collapses all transitions to instant (duration 0).
class AnimationConfig : public QObject
{
    Q_OBJECT
public:
    static AnimationConfig &instance();

    /// Multiplier applied to every authored duration. 1.0 = authored baseline.
    float speed() const { return speed_; }
    /// True disables all transitions (durations collapse to zero).
    bool reduceMotion() const { return reduceMotion_; }

    /// Convenience: scale an authored duration by the current config.
    /// Returns 0 when reduce-motion is on.
    int scaled(int baseMs) const;

    void setSpeed(float speed);
    void setReduceMotion(bool on);

    // Persisted key names (shared with the Appearance settings page).
    static constexpr const char *kSpeedKey = "ui/animationSpeed";
    static constexpr const char *kReduceMotionKey = "ui/reduceMotion";

signals:
    void speedChanged(float speed);
    void reduceMotionChanged(bool on);
    /// Emitted when either setting changes, so live widgets can retune.
    void configChanged();

private:
    AnimationConfig();
    ~AnimationConfig() override = default;

    void load();
    void save() const;

    float speed_ = 1.0f;
    bool reduceMotion_ = false;
};

} // namespace Core
} // namespace Acheron

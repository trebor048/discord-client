#pragma once

#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>
#include <QStyle>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>

namespace Acheron {
namespace Core {

/// Collection of reusable, lightweight widget animations.
/// All animations clean up their effects automatically on completion
/// so that QSS styling (:hover, :checked) is not permanently broken.
/// Overlapping animations are safe: each cleanup verifies it still owns
/// the effect before removing it, preventing one animation from destroying
/// another animation's opacity effect.

namespace AnimationUtils {

/// Stop any running QPropertyAnimation targeting "opacity" on a widget.
/// Used internally so overlapping fades don't fight each other.
inline void stopExistingOpacityAnimations(QWidget *w)
{
    if (!w) return;
    for (auto *a : w->findChildren<QPropertyAnimation *>()) {
        if (a->propertyName() == "opacity") {
            a->stop();
            a->deleteLater();
        }
    }
}

/// Fade a widget from nearly transparent to fully opaque.
/// @param w        Target widget (stored as QPointer — safe if deleted mid-anim).
/// @param ms       Duration in milliseconds (default 150).
/// @param curve    Easing curve (default OutCubic — fast start, slow end).
inline void fadeIn(QWidget *w, int ms = 150, QEasingCurve curve = QEasingCurve::OutCubic)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.01);
    auto *a = new QPropertyAnimation(fx, "opacity", w);
    a->setDuration(ms);
    a->setStartValue(0.01);
    a->setEndValue(1.0);
    a->setEasingCurve(curve);
    QObject::connect(a, &QPropertyAnimation::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect)
            wp->setGraphicsEffect(nullptr);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Fade a widget out and hide it.
/// @param w        Target widget.
/// @param ms       Duration in milliseconds (default 120).
/// @param curve    Easing curve (default InCubic — fast start, slow end).
inline void fadeOut(QWidget *w, int ms = 120, QEasingCurve curve = QEasingCurve::InCubic)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(1.0);
    auto *a = new QPropertyAnimation(fx, "opacity", w);
    a->setDuration(ms);
    a->setStartValue(1.0);
    a->setEndValue(0.0);
    a->setEasingCurve(curve);
    QObject::connect(a, &QPropertyAnimation::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect) {
            wp->setGraphicsEffect(nullptr);
            wp->hide();
        }
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Fade a widget's opacity between two values.
/// The effect is always cleaned up after the animation finishes.
/// @param w         Target widget.
/// @param start     Starting opacity (0.0 – 1.0).
/// @param end       Ending opacity (0.0 – 1.0).
/// @param ms        Duration in milliseconds (default 150).
/// @param curve     Easing curve (default OutCubic).
inline void fadeTo(QWidget *w, qreal start, qreal end, int ms = 150,
                   QEasingCurve curve = QEasingCurve::OutCubic)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(start);
    auto *a = new QPropertyAnimation(fx, "opacity", w);
    a->setDuration(ms);
    a->setStartValue(start);
    a->setEndValue(end);
    a->setEasingCurve(curve);
    QObject::connect(a, &QPropertyAnimation::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect)
            wp->setGraphicsEffect(nullptr);
    });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Quick pop-in: scale from 0.8 → 1.0 while fading in.
/// Uses both opacity and a hidden scale transform via stylesheet.
inline void popIn(QWidget *w, int ms = 200)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    w->setProperty("anim-scale", 0.8);
    w->style()->unpolish(w);
    w->style()->polish(w);

    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.0);

    auto *group = new QParallelAnimationGroup(w);
    auto *fade = new QPropertyAnimation(fx, "opacity", group);
    fade->setDuration(ms);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);
    QObject::connect(group, &QParallelAnimationGroup::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect) {
            wp->setGraphicsEffect(nullptr);
            wp->setProperty("anim-scale", QVariant());
            wp->style()->unpolish(wp);
            wp->style()->polish(wp);
        }
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace AnimationUtils
} // namespace Core
} // namespace Acheron

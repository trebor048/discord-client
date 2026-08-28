#pragma once

#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>
#include <QStyle>
#include <QGraphicsOpacityEffect>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QEvent>
#include <QEasingCurve>

#include <functional>

#include "Core/Animation/AnimationConfig.hpp"

namespace Acheron {
namespace Core {

/// Collection of reusable, lightweight widget animations.
///
/// Every duration is routed through AnimationConfig: the user's animation
/// speed multiplier scales it, and "reduce motion" collapses it to zero so the
/// transition happens instantly. All animations clean up their effects
/// automatically on completion so that QSS styling (:hover, :checked) is not
/// permanently broken. Overlapping animations are safe: each cleanup verifies
/// it still owns the effect before removing it.

namespace AnimationUtils {

/// Config-aware duration: authored baseline scaled by the user's speed,
/// zero when reduce-motion is enabled.
inline int duration(int baseMs)
{
    return AnimationConfig::instance().scaled(baseMs);
}

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
/// @param baseMs   Authored duration in milliseconds (scaled by config).
inline void fadeIn(QWidget *w, int baseMs = 200, QEasingCurve curve = QEasingCurve::OutCubic)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.01);
    auto *a = new QPropertyAnimation(fx, "opacity", w);
    a->setDuration(duration(baseMs));
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
/// @param baseMs   Authored duration in milliseconds (scaled by config).
inline void fadeOut(QWidget *w, int baseMs = 160, QEasingCurve curve = QEasingCurve::InCubic)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(1.0);
    auto *a = new QPropertyAnimation(fx, "opacity", w);
    a->setDuration(duration(baseMs));
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
inline void fadeTo(QWidget *w, qreal start, qreal end, int baseMs = 200,
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
    a->setDuration(duration(baseMs));
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

/// Pop-in: fade from transparent while scaling up from 0.85 → 1.0.
/// Scale is applied through the widget's minimum/maximum width+height so it
/// works on plain widgets inside layouts, then restored on completion.
/// @param w        Target widget.
/// @param baseMs   Authored duration (default 300 — pronounced feel).
inline void popIn(QWidget *w, int baseMs = 300)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.0);

    const QSize finalSize = w->size();
    const QSize startSize(static_cast<int>(finalSize.width() * 0.85),
                          static_cast<int>(finalSize.height() * 0.85));

    auto *group = new QParallelAnimationGroup(w);
    auto *fade = new QPropertyAnimation(fx, "opacity", group);
    fade->setDuration(duration(baseMs));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);

    // Scale via geometry: shrink then restore. Keep the widget centered while
    // it grows by adjusting the position by the size delta.
    const QPoint pos = w->pos();
    auto *scale = new QPropertyAnimation(w, "geometry", group);
    scale->setDuration(duration(baseMs));
    scale->setStartValue(QRect(pos.x() + (finalSize.width() - startSize.width()) / 2,
                               pos.y() + (finalSize.height() - startSize.height()) / 2,
                               startSize.width(), startSize.height()));
    scale->setEndValue(QRect(pos, finalSize));
    scale->setEasingCurve(QEasingCurve::OutBack);
    group->addAnimation(scale);

    QObject::connect(group, &QParallelAnimationGroup::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect)
            wp->setGraphicsEffect(nullptr);
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Slide a widget in from a direction while fading.
/// @param w        Target widget.
/// @param from     Direction the widget comes from (the widget moves INTO view).
/// @param distance How far it travels (default 24 px).
/// @param baseMs   Authored duration (default 250).
inline void slideIn(QWidget *w, Qt::Edge from, int distance = 24, int baseMs = 250)
{
    if (!w) return;
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.0);

    const QPoint finalPos = w->pos();
    QPoint startPos = finalPos;
    switch (from) {
    case Qt::TopEdge:    startPos.ry() -= distance; break;
    case Qt::BottomEdge: startPos.ry() += distance; break;
    case Qt::LeftEdge:   startPos.rx() -= distance; break;
    case Qt::RightEdge:  startPos.rx() += distance; break;
    }

    auto *group = new QParallelAnimationGroup(w);
    auto *fade = new QPropertyAnimation(fx, "opacity", group);
    fade->setDuration(duration(baseMs));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(fade);

    auto *move = new QPropertyAnimation(w, "pos", group);
    move->setDuration(duration(baseMs));
    move->setStartValue(startPos);
    move->setEndValue(finalPos);
    move->setEasingCurve(QEasingCurve::OutCubic);
    group->addAnimation(move);

    QObject::connect(group, &QParallelAnimationGroup::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect)
            wp->setGraphicsEffect(nullptr);
    });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Standard popup entry: fade in.
///
/// Opacity-only by design: `w` is typically a layout-managed child (e.g. the
/// fadeHost of BasePopup), so animating geometry/pos fights the layout and
/// starts from a stale rect captured before layout settles — which made the
/// popup jump. A pure opacity fade is layout-independent and always correct.
inline void popupEnter(QWidget *w, int baseMs = 300)
{
    if (!w) return;
    if (AnimationConfig::instance().reduceMotion()) {
        w->show();
        return;
    }
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(0.0);

    auto *fade = new QPropertyAnimation(fx, "opacity", w);
    fade->setDuration(duration(baseMs));
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(fade, &QPropertyAnimation::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx)]() {
        if (wp && wp->graphicsEffect() == effect)
            wp->setGraphicsEffect(nullptr);
    });
    w->show();
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Standard popup exit: fade out, then hide.
/// @param w           Target widget.
/// @param onFinished  Called after the widget is hidden (e.g. accept()/reject()).
/// @param baseMs      Authored duration (default 180).
inline void popupExit(QWidget *w, std::function<void()> onFinished = {}, int baseMs = 180)
{
    if (!w) return;
    if (AnimationConfig::instance().reduceMotion()) {
        w->hide();
        if (onFinished) onFinished();
        return;
    }
    stopExistingOpacityAnimations(w);
    auto *oldFx = w->graphicsEffect();
    if (oldFx) oldFx->deleteLater();
    auto *fx = new QGraphicsOpacityEffect(w);
    w->setGraphicsEffect(fx);
    fx->setOpacity(1.0);

    auto *fade = new QPropertyAnimation(fx, "opacity", w);
    fade->setDuration(duration(baseMs));
    fade->setStartValue(1.0);
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(fade, &QPropertyAnimation::finished, w,
                     [wp = QPointer(w), effect = QPointer(fx), onFinished]() {
        if (wp) {
            if (wp->graphicsEffect() == effect)
                wp->setGraphicsEffect(nullptr);
            wp->hide();
            if (onFinished)
                onFinished();
        }
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

} // namespace AnimationUtils
} // namespace Core
} // namespace Acheron

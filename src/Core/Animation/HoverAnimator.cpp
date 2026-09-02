#include "Core/Animation/HoverAnimator.hpp"

#include "Core/Animation/AnimationConfig.hpp"
#include "Core/Theme/Manager.hpp"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHoverEvent>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QSlider>
#include <QTabBar>
#include <QWidget>

#include <algorithm>

namespace Acheron {
namespace Core {

HoverAnimator &HoverAnimator::instance()
{
    static HoverAnimator animator;
    return animator;
}

HoverAnimator::HoverAnimator(QObject *parent)
    : QObject(parent)
{
    // If reduce-motion is toggled ON while a wash is visible, hover/leave
    // events stop being processed (eventFilterForWidget bails out early), so
    // nothing would ever hide the lingering overlay. Flush all washes the
    // moment the flag flips.
    connect(&AnimationConfig::instance(), &AnimationConfig::reduceMotionChanged, this,
            [this](bool on) {
                if (!on)
                    return;
                const auto widgets = states_.keys();
                for (QWidget *w : widgets)
                    removeState(w);
            });
}

void HoverAnimator::install()
{
    if (installed_)
        return;
    installed_ = true;
    qApp->installEventFilter(this);
}

namespace {

bool isWashEligible(QWidget *w)
{
    if (qobject_cast<QAbstractButton *>(w) || qobject_cast<QComboBox *>(w)
        || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QSlider *>(w)
        || qobject_cast<QTabBar *>(w))
        return true;

    // Item-view viewports: hover row washes.
    if (auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget())) {
        if (view->viewport() == w)
            return true;
    }
    return false;
}

/// Resolve the wash color from the theme: a translucent accent highlight
/// tinted by the theme's highlight color, honoring the palette.
QColor washColor()
{
    const QColor highlight = Core::Theme::Manager::instance().color(Core::Theme::Token::Highlight);
    QColor c = highlight;
    c.setAlpha(55); // ~22% alpha wash — clearly visible but not opaque
    return c;
}

int washRoundness(QWidget *w)
{
    if (qobject_cast<QAbstractItemView *>(w->parentWidget()))
        return 4;
    return std::max(2, Core::Theme::Manager::instance().roundness() / 2);
}

/// The widget the wash overlay is a child of (viewport for item views, the
/// widget itself otherwise) — used to compute the wash's corner radius.
QWidget *hostFor(QWidget *w)
{
    if (auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget());
        view && view->viewport() == w)
        return w;
    return w;
}

} // namespace

bool HoverAnimator::eventFilter(QObject *watched, QEvent *event)
{
    if (!watched || !watched->isWidgetType())
        return QObject::eventFilter(watched, event);

    auto *w = static_cast<QWidget *>(watched);
    switch (event->type()) {
    case QEvent::HoverEnter:
    case QEvent::HoverMove:
        return eventFilterForWidget(w, event);
    case QEvent::HoverLeave:
    case QEvent::Leave:
        return eventFilterForWidget(w, event);
    case QEvent::Enter:
        // Enter fires for widgets without WA_Hover; treat as hover enter.
        return eventFilterForWidget(w, event);
    case QEvent::Scroll:
        // Re-track the hovered row after scrolling (the wash would otherwise
        // stick to the stale item rect while the mouse stays still).
        if (states_.contains(w))
            onHoverMove(w, states_.value(w).lastHoverPos);
        break;
    case QEvent::MouseButtonPress:
        // Press feedback: flash the wash to a deeper tone quickly. Respect
        // reduce-motion: onPress would otherwise create a wash even though the
        // hover path is disabled.
        if (isWashEligible(w) && !AnimationConfig::instance().reduceMotion())
            onPress(w, true);
        break;
    case QEvent::MouseButtonRelease:
        if (isWashEligible(w) && !AnimationConfig::instance().reduceMotion())
            onPress(w, false);
        break;
    case QEvent::Destroy:
        removeState(w);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

bool HoverAnimator::eventFilterForWidget(QWidget *w, QEvent *event)
{
    if (AnimationConfig::instance().reduceMotion())
        return QObject::eventFilter(w, event);

    if (!isWashEligible(w))
        return QObject::eventFilter(w, event);

    switch (event->type()) {
    case QEvent::HoverEnter:
        onHoverEnter(w);
        break;
    case QEvent::HoverMove: {
        auto *he = static_cast<QHoverEvent *>(event);
        onHoverMove(w, he->position().toPoint());
        break;
    }
    case QEvent::HoverLeave:
        onHoverLeave(w);
        break;
    // Enter/Leave fire for widgets without WA_Hover; when WA_Hover is set,
    // HoverEnter/HoverLeave are also delivered and would double-trigger the
    // wash (restarting the fade). Use Enter/Leave only as the no-WA_Hover path.
    case QEvent::Enter:
        if (!w->testAttribute(Qt::WA_Hover))
            onHoverEnter(w);
        break;
    case QEvent::Leave:
        if (!w->testAttribute(Qt::WA_Hover))
            onHoverLeave(w);
        break;
    default:
        break;
    }
    return QObject::eventFilter(w, event);
}

void HoverAnimator::onHoverEnter(QWidget *w)
{
    // Item views: the wash follows the hovered row; geometry is updated on
    // HoverMove, so just ensure a wash exists (empty rect until first move).
    QWidget *host = w;
    QRect itemRect;
    bool isItem = false;
    if (auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget());
        view && view->viewport() == w) {
        host = w; // viewport: visualRect() coordinates are viewport-relative
        isItem = true;
    }

    auto it = states_.find(w);
    if (it == states_.end()) {
        WashState st;
        st.isItemView = isItem;
        st.target = w;

        // Hook destroyed to clean hash entry: QEvent::Destroy is not delivered
        // to event filters observing another object, so the previous Destroy
        // case never fired and left dangling keys.
        connect(w, &QObject::destroyed, this, [this, w]() { removeState(w); },
                Qt::UniqueConnection);

        auto *overlay = new QWidget(host);
        overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        // A plain QWidget does not paint a stylesheet background unless
        // WA_StyledBackground is set; without it the wash never renders.
        overlay->setAttribute(Qt::WA_StyledBackground, true);
        overlay->setStyleSheet(QStringLiteral(
                "QWidget { background-color: %1; border-radius: %2px; }")
                                       .arg(washColor().name(QColor::HexArgb))
                                       .arg(washRoundness(host)));
        auto *fx = new QGraphicsOpacityEffect(overlay);
        overlay->setGraphicsEffect(fx);
        fx->setOpacity(0.0);
        overlay->show();
        st.overlay = overlay;

        it = states_.insert(w, st);
    }

    WashState &st = it.value();
    if (st.anim) {
        st.anim->stop();
        st.anim->deleteLater();
        st.anim = nullptr;
    }

    if (st.isItemView) {
        // Item-view viewports do not set WA_Hover by default, so only Enter
        // fires and no HoverMove would ever reach us. Enable it so the wash
        // can track rows, and seed the initial rect from the cursor position.
        w->setAttribute(Qt::WA_Hover, true);
        const QPoint local = w->mapFromGlobal(QCursor::pos());
        st.lastHoverPos = local;
        if (auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget())) {
            const QModelIndex idx = view->indexAt(local);
            if (idx.isValid())
                st.itemRect = view->visualRect(idx);
        }
        if (st.itemRect.isValid())
            st.overlay->setGeometry(st.itemRect);
    } else {
        st.overlay->setGeometry(w->rect());
        st.overlay->raise();
    }

    // onHoverLeave's fade-out hides the overlay; the reuse path (state already
    // exists) must re-show it or the wash fades in invisibly and stays dead
    // for every hover after the first leave.
    st.overlay->show();

    auto *fadeIn = new QPropertyAnimation(st.overlay->graphicsEffect(), "opacity", st.overlay);
    fadeIn->setDuration(AnimationConfig::instance().scaled(180));
    fadeIn->setStartValue(st.overlay->graphicsEffect()->property("opacity").toReal());
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);
    st.anim = fadeIn;
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
}

void HoverAnimator::onHoverMove(QWidget *w, const QPoint &pos)
{
    auto *view = qobject_cast<QAbstractItemView *>(w->parentWidget());
    if (!view || view->viewport() != w)
        return;
    auto it = states_.find(w);
    if (it == states_.end())
        return;

    const QModelIndex idx = view->indexAt(pos);
    QRect rect;
    if (idx.isValid())
        rect = view->visualRect(idx);

    WashState &st = it.value();
    st.lastHoverPos = pos;
    // The wash only needs re-geometry when the hovered row actually changes;
    // mouse-move events within the same row must not churn the overlay (each
    // setGeometry invalidates old+new areas and forces delegate repaints).
    if (rect == st.itemRect)
        return;
    st.itemRect = rect;
    if (st.overlay) {
        st.overlay->setGeometry(rect);
        st.overlay->raise();
    }
}

void HoverAnimator::onHoverLeave(QWidget *w)
{
    auto it = states_.find(w);
    if (it == states_.end())
        return;

    WashState &st = it.value();
    if (!st.overlay)
        return;
    if (st.anim) {
        st.anim->stop();
        st.anim->deleteLater();
        st.anim = nullptr;
    }

    auto *fadeOut = new QPropertyAnimation(st.overlay->graphicsEffect(), "opacity", st.overlay);
    fadeOut->setDuration(AnimationConfig::instance().scaled(140));
    fadeOut->setStartValue(st.overlay->graphicsEffect()->property("opacity").toReal());
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    QObject::connect(fadeOut, &QPropertyAnimation::finished, st.overlay,
                     [overlay = st.overlay]() {
                         overlay->hide();
                     });
    st.anim = fadeOut;
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
}

/// Press feedback: deepen the wash quickly on press, ease back to the hover
/// level on release. Non-hovered widgets (e.g. pressed via keyboard) start
/// from 0 and fade to the pressed level.
void HoverAnimator::onPress(QWidget *w, bool pressed)
{
    auto it = states_.find(w);
    if (it == states_.end()) {
        if (!pressed)
            return;
        // Keyboard activation / no prior hover: create a wash so the press
        // flash is still visible.
        onHoverEnter(w);
        it = states_.find(w);
        if (it == states_.end())
            return;
    }

    WashState &st = it.value();
    if (!st.overlay)
        return;

    // Press deepens the wash color; release restores the hover wash. A color
    // change cannot be animated by QSS, so fade opacity out then back in
    // around the color swap for a smooth two-step feel.
    const QColor base = washColor();
    QColor pressedColor = base;
    pressedColor.setAlpha(std::min(255, base.alpha() + 60));

    auto setWashStyle = [roundness = washRoundness(hostFor(w))](QWidget *overlay, const QColor &c) {
        overlay->setStyleSheet(QStringLiteral(
                "QWidget { background-color: %1; border-radius: %2px; }")
                                       .arg(c.name(QColor::HexArgb))
                                       .arg(roundness));
    };

    if (st.anim) {
        st.anim->stop();
        st.anim->deleteLater();
        st.anim = nullptr;
    }

    auto *fx = qobject_cast<QGraphicsOpacityEffect *>(st.overlay->graphicsEffect());
    if (!fx)
        return;
    const qreal from = fx->opacity();

    // Two quick fades: drop to 0, swap color, rise to the target opacity.
    auto *seq = new QSequentialAnimationGroup(st.overlay);
    auto *down = new QPropertyAnimation(fx, "opacity", seq);
    down->setDuration(AnimationConfig::instance().scaled(pressed ? 50 : 60));
    down->setStartValue(from);
    down->setEndValue(0.0);
    down->setEasingCurve(QEasingCurve::InQuad);
    seq->addAnimation(down);

    QObject::connect(down, &QPropertyAnimation::finished, st.overlay,
                     [overlay = st.overlay, setWashStyle, pressed, base, pressedColor]() {
                         setWashStyle(overlay, pressed ? pressedColor : base);
                     });

    auto *up = new QPropertyAnimation(fx, "opacity", seq);
    up->setDuration(AnimationConfig::instance().scaled(pressed ? 60 : 200));
    up->setStartValue(0.0);
    up->setEndValue(1.0);
    up->setEasingCurve(pressed ? QEasingCurve::OutQuad : QEasingCurve::OutCubic);
    seq->addAnimation(up);

    if (!pressed) {
        QObject::connect(seq, &QSequentialAnimationGroup::finished, st.overlay,
                         [overlay = st.overlay]() {
                             if (!overlay->underMouse())
                                 overlay->hide();
                         });
    }
    st.anim = seq;
    seq->start(QAbstractAnimation::DeleteWhenStopped);
}

void HoverAnimator::removeState(QWidget *w)
{
    auto it = states_.find(w);
    if (it == states_.end())
        return;
    if (it.value().anim) {
        it.value().anim->stop();
        it.value().anim->deleteLater();
    }
    if (it.value().overlay)
        it.value().overlay->deleteLater();
    states_.erase(it);
}

} // namespace Core
} // namespace Acheron

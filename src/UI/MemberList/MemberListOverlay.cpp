#include "MemberListOverlay.hpp"

#include "MemberListView.hpp"
#include "MemberListDelegate.hpp"
#include "Core/AnimationUtils.hpp"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QTimer>

#include <algorithm>

namespace Acheron {
namespace UI {

namespace {
constexpr int kExpandMs = 200;
constexpr int kCollapseMs = 160;
constexpr int kCollapseDelayMs = 200;
} // namespace

MemberListOverlay::MemberListOverlay(MemberListView *view, QWidget *parent)
    : QWidget(parent)
    , view_(view)
{
    view_->setOverlayMode(true);
    view_->setParent(this);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(view_);

    collapseTimer_ = new QTimer(this);
    collapseTimer_->setSingleShot(true);
    collapseTimer_->setInterval(kCollapseDelayMs);
    connect(collapseTimer_, &QTimer::timeout, this, &MemberListOverlay::collapseNow);

    if (QWidget *p = parentWidget())
        p->installEventFilter(this);

    reposition();
    applyIconsOnly(true);
}

MemberListOverlay::~MemberListOverlay()
{
    // The delegate is owned by MemberListView and outlives the overlay. Reset
    // the overlay-only state (icons-only strip + content fade) so a later
    // SlideOut -> ResizeHandle switch does not leave the docked member list
    // showing only avatars at ~0 opacity.
    if (auto *delegate = qobject_cast<MemberListDelegate *>(view_->itemDelegate())) {
        delegate->setIconsOnly(false);
        delegate->setContentOpacity(1.0);
    }
}

qreal MemberListOverlay::contentOpacityForWidth(int width)
{
    const int span = kExpandedWidth - kCollapsedWidth;
    if (span <= 0)
        return 1.0;
    return std::clamp(qreal(width - kCollapsedWidth) / span, 0.0, 1.0);
}

void MemberListOverlay::expand()
{
    collapseTimer_->stop();
    if (expanded_)
        return;
    expanded_ = true;
    // One motion: switch to full content mode and animate the width; the
    // animation's valueChanged drives the content opacity up in lockstep, so
    // nothing pops in after the panel finishes sliding.
    if (auto *delegate = qobject_cast<MemberListDelegate *>(view_->itemDelegate())) {
        delegate->setIconsOnly(false);
        delegate->setContentOpacity(0.0);
    }
    view_->viewport()->update();
    animateWidth(kExpandedWidth);
}

void MemberListOverlay::collapse()
{
    if (!expanded_)
        return;
    collapseTimer_->start();
}

void MemberListOverlay::collapseNow()
{
    if (!expanded_)
        return;
    expanded_ = false;
    // One motion: the width slides back and the content opacity follows it
    // down; the finished handler switches to icons-only for a clean rest.
    animateWidth(kCollapsedWidth);
}

void MemberListOverlay::enterEvent(QEnterEvent *event)
{
    QWidget::enterEvent(event);
    expand();
}

void MemberListOverlay::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    collapse();
}

void MemberListOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // While the width animation runs, every animated geometry change triggers a
    // resize; reposition() would snap the overlay back to the full target each
    // frame and defeat the slide. Only reposition on non-animated resizes.
    if (!widthAnimation_ || widthAnimation_->state() != QAbstractAnimation::Running)
        reposition();
}

void MemberListOverlay::animateWidth(int targetWidth)
{
    if (widthAnimation_) {
        widthAnimation_->stop();
        widthAnimation_->deleteLater();
        widthAnimation_ = nullptr;
    }

    const bool expanding = targetWidth > width();
    auto *anim = new QPropertyAnimation(this, "geometry", this);
    anim->setDuration(Core::AnimationUtils::duration(expanding ? kExpandMs : kCollapseMs));
    anim->setStartValue(overlayRect(width()));
    anim->setEndValue(overlayRect(targetWidth));
    anim->setEasingCurve(expanding ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    // The width drives the content opacity frame by frame: one motion, no
    // two-phase reveal after the panel finishes sliding.
    connect(anim, &QPropertyAnimation::valueChanged, this, [this](const QVariant &v) {
        setContentOpacityForWidth(v.toRect().width());
    });
    widthAnimation_ = anim;
    // Capture the specific animation so a newer animation that supersedes this
    // one (rapid hover in/out) is never cleared or deleted by a stale finished
    // signal from the older run.
    connect(anim, &QPropertyAnimation::finished, this, [this, anim]() {
        if (widthAnimation_ == anim)
            widthAnimation_ = nullptr;
        anim->deleteLater();
        if (!expanded_) {
            applyIconsOnly(true);
        } else {
            // Exactly full width -> exactly visible; make the final state
            // deterministic even if the easing ended a hair short.
            setContentOpacityForWidth(kExpandedWidth);
        }
    });
    anim->start();
}

QRect MemberListOverlay::overlayRect(int width) const
{
    QWidget *p = parentWidget();
    const int parentWidth = p ? p->width() : width;
    const int parentHeight = p ? p->height() : height();
    return QRect(parentWidth - width, 0, width, parentHeight);
}

void MemberListOverlay::setContentOpacityForWidth(int width)
{
    if (auto *delegate = qobject_cast<MemberListDelegate *>(view_->itemDelegate()))
        delegate->setContentOpacity(contentOpacityForWidth(width));
    view_->viewport()->update();
}

void MemberListOverlay::applyIconsOnly(bool iconsOnly)
{
    if (auto *delegate = qobject_cast<MemberListDelegate *>(view_->itemDelegate()))
        delegate->setIconsOnly(iconsOnly);
    view_->viewport()->update();
}

void MemberListOverlay::reposition()
{
    setGeometry(overlayRect(expanded_ ? kExpandedWidth : kCollapsedWidth));
}

bool MemberListOverlay::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == parentWidget() && event->type() == QEvent::Resize) {
        // Match resizeEvent(): never snap the geometry while the width
        // animation is running, or a window resize mid-slide would jump the
        // overlay to the full target instead of continuing the slide.
        if (!widthAnimation_ || widthAnimation_->state() != QAbstractAnimation::Running)
            reposition();
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace UI
} // namespace Acheron

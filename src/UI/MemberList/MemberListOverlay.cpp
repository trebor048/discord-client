#include "MemberListOverlay.hpp"

#include "MemberListView.hpp"
#include "MemberListDelegate.hpp"
#include "Core/AnimationUtils.hpp"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QPropertyAnimation>
#include <QTimer>

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

void MemberListOverlay::expand()
{
    collapseTimer_->stop();
    if (expanded_)
        return;
    expanded_ = true;
    applyIconsOnly(false);
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
    applyIconsOnly(true);
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
    reposition();
}

void MemberListOverlay::animateWidth(int targetWidth)
{
    if (widthAnimation_) {
        widthAnimation_->stop();
        widthAnimation_->deleteLater();
    }

    const bool expanding = targetWidth > width();
    widthAnimation_ = new QPropertyAnimation(this, "geometry", this);
    widthAnimation_->setDuration(Core::AnimationUtils::duration(expanding ? kExpandMs : kCollapseMs));
    widthAnimation_->setStartValue(overlayRect(width()));
    widthAnimation_->setEndValue(overlayRect(targetWidth));
    widthAnimation_->setEasingCurve(expanding ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    connect(widthAnimation_, &QPropertyAnimation::finished, this,
            [this]() { widthAnimation_->deleteLater(); widthAnimation_ = nullptr; });
    widthAnimation_->start();
}

QRect MemberListOverlay::overlayRect(int width) const
{
    QWidget *p = parentWidget();
    const int parentWidth = p ? p->width() : width;
    const int parentHeight = p ? p->height() : height();
    return QRect(parentWidth - width, 0, width, parentHeight);
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
    if (obj == parentWidget() && event->type() == QEvent::Resize)
        reposition();
    return QWidget::eventFilter(obj, event);
}

} // namespace UI
} // namespace Acheron

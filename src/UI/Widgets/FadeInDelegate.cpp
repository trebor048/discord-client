#include "FadeInDelegate.hpp"

#include "Core/Animation/AnimationConfig.hpp"

#include <QAbstractItemView>
#include <QPainter>
#include <QVariantAnimation>

namespace Acheron {
namespace UI {

FadeInDelegate::FadeInDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void FadeInDelegate::ensureAnimation()
{
    if (animation)
        return;
    animation = new QVariantAnimation(this);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setDuration(Core::AnimationConfig::instance().scaled(240));
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        onTick(v.toReal());
    });
}

void FadeInDelegate::fadeInRows(int start, int end)
{
    if (end < start)
        return;

    ensureAnimation();
    animation->stop();
    for (int row = start; row <= end; ++row) {
        fadingOut.remove(row);
        pendingDone.remove(row);
        rowOpacity.insert(row, 0.0);
    }
    animation->start();
}

void FadeInDelegate::fadeInAll(int rowCount)
{
    if (rowCount <= 0)
        return;
    fadeInRows(0, rowCount - 1);
}

void FadeInDelegate::fadeOutRow(int row, std::function<void()> done)
{
    ensureAnimation();
    fadingOut.insert(row);
    rowOpacity.insert(row, 1.0);
    pendingDone.insert(row, std::move(done));

    // Restart if not already animating so the fade actually runs; completion
    // is handled in onTick(), not via per-call `finished` connections (those
    // accumulated and fired stale callbacks for rows whose fade had been
    // superseded by a later fade-in/out, removing the wrong item).
    if (animation->state() != QAbstractAnimation::Running)
        animation->start();
}

void FadeInDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const
{
    auto it = rowOpacity.constFind(index.row());
    if (it == rowOpacity.constEnd() || it.value() >= 1.0) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    painter->save();
    painter->setOpacity(std::max(0.0, it.value()));
    QStyledItemDelegate::paint(painter, option, index);
    painter->restore();
}

void FadeInDelegate::onTick(qreal value)
{
    // Rows fading in go 0 -> 1; rows fading out go 1 -> 0.
    for (auto it = rowOpacity.begin(); it != rowOpacity.end(); ++it) {
        if (fadingOut.contains(it.key()))
            it.value() = 1.0 - value;
        else
            it.value() = value;
    }
    if (auto *view = qobject_cast<QAbstractItemView *>(parent()))
        view->viewport()->update();

    if (value >= 1.0) {
        // Complete fade-outs: collect their callbacks, drop the tracking state,
        // then invoke the callbacks (which remove rows from the model) last, so
        // we never mutate the model while iterating the delegate's own maps.
        QList<std::function<void()>> completed;
        for (auto it = fadingOut.begin(); it != fadingOut.end();) {
            const int row = *it;
            rowOpacity.remove(row);
            auto done = pendingDone.take(row);
            if (done)
                completed.append(std::move(done));
            it = fadingOut.erase(it);
        }

        // Fade-in rows that reached full opacity no longer need tracking.
        for (auto it = rowOpacity.begin(); it != rowOpacity.end();) {
            if (!fadingOut.contains(it.key()))
                it = rowOpacity.erase(it);
            else
                ++it;
        }

        if (auto *view = qobject_cast<QAbstractItemView *>(parent()))
            view->viewport()->update();

        for (auto &done : completed)
            done();
    }
}

} // namespace UI
} // namespace Acheron

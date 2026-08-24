#include "MemberListView.hpp"
#include "Core/AnimationUtils.hpp"

namespace Acheron {
namespace UI {

MemberListView::MemberListView(QWidget *parent)
    : QListView(parent)
{
    setObjectName("MemberList");

    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setUniformItemSizes(false);
    setContextMenuPolicy(Qt::CustomContextMenu);

    setFrameShape(QFrame::NoFrame);
    viewport()->setBackgroundRole(QPalette::Window);
    viewport()->setAutoFillBackground(true);
}

void MemberListView::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    if (!overlayMode_ && !hasBeenShown) {
        hasBeenShown = true;
        Core::AnimationUtils::fadeIn(this, 250);
    }
}

void MemberListView::scrollContentsBy(int dx, int dy)
{
    QListView::scrollContentsBy(dx, dy);
    emitVisibleRange();
}

void MemberListView::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    emitVisibleRange();
}

void MemberListView::emitVisibleRange()
{
    if (!model() || model()->rowCount() == 0)
        return;

    QModelIndex topIndex = indexAt(viewport()->rect().topLeft());
    QModelIndex bottomIndex = indexAt(viewport()->rect().bottomLeft());

    int first = topIndex.isValid() ? topIndex.row() : 0;
    int last = bottomIndex.isValid() ? bottomIndex.row() : model()->rowCount() - 1;

    if (first != lastFirstVisible || last != lastLastVisible) {
        lastFirstVisible = first;
        lastLastVisible = last;
        emit visibleRangeChanged(first, last);
    }
}

} // namespace UI
} // namespace Acheron

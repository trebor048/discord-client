#include "SplitterHandle.hpp"
#include <QPainter>
#include <QSplitter>

namespace Acheron {
namespace UI {

SplitterHandle::SplitterHandle(QSplitter *parent)
    : QSplitterHandle(parent->orientation(), parent)
{
    if (parent->orientation() == Qt::Horizontal)
        setFixedWidth(4);
    else
        setFixedHeight(4);
}

void SplitterHandle::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QColor lineColor = palette().color(QPalette::Mid);
    if (hovered)
        lineColor = lineColor.lighter(130);

    Qt::Orientation orient = orientation();
    if (orient == Qt::Horizontal) {
        int cx = width() / 2;
        int half = qMin(1, height() / 6);
        p.setPen(QPen(lineColor, 2));
        p.drawLine(cx, half, cx, height() - half);
    } else {
        int cy = height() / 2;
        int half = qMin(1, width() / 6);
        p.setPen(QPen(lineColor, 2));
        p.drawLine(half, cy, width() - half, cy);
    }
}

void SplitterHandle::enterEvent(QEnterEvent *)
{
    hovered = true;
    update();
}

void SplitterHandle::leaveEvent(QEvent *)
{
    hovered = false;
    update();
}

} // namespace UI
} // namespace Acheron

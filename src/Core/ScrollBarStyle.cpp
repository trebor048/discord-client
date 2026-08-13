#include "ScrollBarStyle.hpp"
#include <QPainter>
#include <QStyleOptionSlider>

namespace Acheron {
namespace Core {

ScrollBarStyle::ScrollBarStyle(QStyle *base) : QProxyStyle(base)
{
}

int ScrollBarStyle::styleHint(StyleHint hint, const QStyleOption *option,
                               const QWidget *widget, QStyleHintReturn *returnData) const
{
    if (hint == SH_ScrollBar_Transient)
        return 0;
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

QRect ScrollBarStyle::subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                                      SubControl subControl, const QWidget *widget) const
{
    if (control != CC_ScrollBar)
        return QProxyStyle::subControlRect(control, option, subControl, widget);

    auto *scrollOpt = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (!scrollOpt)
        return QProxyStyle::subControlRect(control, option, subControl, widget);

    if (subControl == SC_ScrollBarSubLine || subControl == SC_ScrollBarAddLine)
        return {};

    if (subControl == SC_ScrollBarSlider) {
        QRect r = QProxyStyle::subControlRect(control, option, subControl, widget);
        int handleWidth = (option->state & State_MouseOver) ? 12 : 6;
        if (scrollOpt->orientation == Qt::Vertical) {
            int pad = (scrollOpt->rect.width() - handleWidth) / 2;
            r.setLeft(scrollOpt->rect.left() + pad);
            r.setWidth(handleWidth);
        } else {
            int pad = (scrollOpt->rect.height() - handleWidth) / 2;
            r.setTop(scrollOpt->rect.top() + pad);
            r.setHeight(handleWidth);
        }
        return r;
    }

    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QSize ScrollBarStyle::sizeFromContents(ContentsType type, const QStyleOption *option,
                                        const QSize &contentsSize, const QWidget *widget) const
{
    if (type == CT_ScrollBar) {
        auto *scrollOpt = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (scrollOpt && scrollOpt->orientation == Qt::Vertical)
            return QSize(6, contentsSize.height());
        else
            return QSize(contentsSize.width(), 6);
    }
    return QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
}

void ScrollBarStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                         QPainter *painter, const QWidget *widget) const
{
    if (control != CC_ScrollBar) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    auto *scrollOpt = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (!scrollOpt) {
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    QProxyStyle::drawComplexControl(control, option, painter, widget);

    QRect sliderRect = subControlRect(CC_ScrollBar, option, SC_ScrollBarSlider, widget);
    if (sliderRect.isEmpty())
        return;

    bool hovered = option->state & State_MouseOver;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QColor handleColor = widget ? widget->palette().color(QPalette::Mid)
                                : QColor(128, 128, 128);
    handleColor.setAlpha(hovered ? 180 : 80);

    painter->setBrush(handleColor);
    painter->setPen(Qt::NoPen);

    int radius = qMin(sliderRect.width(), sliderRect.height()) / 2;
    painter->drawRoundedRect(sliderRect, radius, radius);

    painter->restore();
}

} // namespace Core
} // namespace Acheron

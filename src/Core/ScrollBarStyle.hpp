#pragma once

#include <QProxyStyle>

class QWidget;

namespace Acheron {
namespace Core {

class ScrollBarStyle : public QProxyStyle
{
    Q_OBJECT
public:
    explicit ScrollBarStyle(QStyle *base = nullptr);

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr, QStyleHintReturn *returnData = nullptr) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option,
                           const QSize &contentsSize, const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
};

} // namespace Core
} // namespace Acheron

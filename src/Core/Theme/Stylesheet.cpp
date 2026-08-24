#include "Core/Theme/Stylesheet.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>

#include <algorithm>
#include <cmath>

namespace Acheron {
namespace Core {
namespace Theme {

namespace {
QString hex(const QColor &c)
{
    return c.name(c.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb);
}
} // namespace

QString buildStyleSheet()
{
    const Manager &m = Manager::instance();

    const QColor baseBg = m.color(Token::BaseBg);
    const QColor windowBg = m.color(Token::WindowBg);
    const QColor tooltipBg = m.color(Token::TooltipBg);
    const QColor tooltipText = m.color(Token::TooltipText);
    const QColor divider = m.color(Token::Divider);
    const QColor highlight = m.color(Token::Highlight);

    const int r = m.roundness();
    const int rSmall = std::max(2, r / 2);

    QString qss;

    constexpr qreal tooltipFontScale = 0.9;
    QString tooltipFontSize;
    const qreal uiPointSize = m.font(FontRole::Ui).pointSizeF();
    if (uiPointSize > 0)
        tooltipFontSize = QStringLiteral("  font-size: %1pt;").arg(uiPointSize * tooltipFontScale);

    qss += QStringLiteral("QToolTip {"
                          "  background-color: %1;"
                          "  color: %2;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4px;"
                          "  padding: 2px 6px;"
                          "%5"
                          "}")
                   .arg(hex(tooltipBg), hex(tooltipText), hex(divider))
                   .arg(rSmall)
                   .arg(tooltipFontSize);

    const QColor scrollbarHandle = divider.darker(120);
    const QColor scrollbarHandleHover = highlight.lighter(120);

    qss += QStringLiteral("QScrollBar:vertical {"
                          "  background: transparent;"
                          "  width: 8px;"
                          "  margin: 0;"
                          "}"
                          "QScrollBar::handle:vertical {"
                          "  background-color: %1;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4px;"
                          "  min-height: 30px;"
                          "  margin: 1px;"
                          "}"
                          "QScrollBar::handle:vertical:hover {"
                          "  background-color: %2;"
                          "}"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                          "  height: 0; width: 0;"
                          "}"
                          "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
                          "  background: none;"
                          "}"
                          "QScrollBar:horizontal {"
                          "  background: transparent;"
                          "  height: 8px;"
                          "  margin: 0;"
                          "}"
                          "QScrollBar::handle:horizontal {"
                          "  background-color: %1;"
                          "  border: 1px solid %3;"
                          "  border-radius: %4px;"
                          "  min-width: 30px;"
                          "  margin: 1px;"
                          "}"
                          "QScrollBar::handle:horizontal:hover {"
                          "  background-color: %2;"
                          "}"
                          "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
                          "  height: 0; width: 0;"
                          "}"
                          "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
                          "  background: none;"
                          "}")
                   .arg(hex(scrollbarHandle), hex(scrollbarHandleHover), hex(windowBg))
                   .arg(rSmall);

    qss += QStringLiteral("#MessageInput {"
                          "  background-color: %1;"
                          "  border: 1px solid %2;"
                          "  border-radius: %4px;"
                          "  padding: 8px 10px; }"
                          "#MessageInput:focus { border: 1px solid %3; }")
                   .arg(hex(baseBg), hex(divider), hex(highlight))
                   .arg(r);

    // Apply the global corner radius to the common widget families that do
    // not set their own inline border-radius, so the "roundness" setting
    // visibly shapes the whole app rather than just tooltips and scrollbars.
    qss += QStringLiteral("QPushButton, QToolButton, QComboBox, QSpinBox, QDoubleSpinBox,"
                          " QLineEdit, QTextEdit, QPlainTextEdit,"
                          " QListWidget, QListView, QTreeView, QTableView,"
                          " QGroupBox, QFrame, QMenu {"
                          "  border-radius: %1px; }")
                   .arg(r);

    // Group boxes: reserve room for the title so it never collides with the
    // first child (visible at large UI fonts), and give the contents generous
    // padding. `margin-top` lifts the title above the box border; `padding-top`
    // pushes the content below it. Both scale with the UI font size so the
    // title keeps its own row at any font size.
    const qreal uiPt = uiPointSize > 0 ? uiPointSize : 9.0;
    const int groupTitleMargin = static_cast<int>(std::lround(uiPt * 1.5)) + 4;
    const int groupTitlePad = static_cast<int>(std::lround(uiPt * 0.6)) + 4;
    const int groupSidePad = std::max(10, static_cast<int>(std::lround(uiPt * 0.8)) + 2);
    qss += QStringLiteral("QGroupBox {"
                          "  margin-top: %1px;"
                          "  padding-top: %2px;"
                          "  padding-left: %3px;"
                          "  padding-right: %3px;"
                          "  padding-bottom: %3px; }"
                          "QGroupBox::title {"
                          "  subcontrol-origin: margin;"
                          "  left: 10px;"
                          "  padding: 0 6px;"
                          "  color: %4; }")
                   .arg(groupTitleMargin)
                   .arg(groupTitlePad)
                   .arg(groupSidePad)
                   .arg(hex(highlight));

    return qss;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

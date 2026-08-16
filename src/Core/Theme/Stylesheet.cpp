#include "Core/Theme/Stylesheet.hpp"

#include "Core/Theme/Fonts.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"

#include <QColor>

#include <algorithm>

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

    return qss;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

#include "CustomTitleBar.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWindow>

namespace Acheron {
namespace UI {

namespace {
constexpr int kGlyphInset = 3; // glyph length inside the 12px button
} // namespace

CustomTitleBar::CustomTitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

QRect CustomTitleBar::buttonRect(Button button) const
{
    if (button == Button::None)
        return {};
    const int index = button == Button::Close   ? 0
                      : button == Button::Minimize ? 1
                                                   : 2;
    const int x = kLeftMargin + index * (kButtonDiameter + kButtonSpacing);
    const int y = (height() - kButtonDiameter) / 2;
    return QRect(x, y, kButtonDiameter, kButtonDiameter);
}

CustomTitleBar::Button CustomTitleBar::buttonAt(const QPoint &pos) const
{
    if (buttonRect(Button::Close).contains(pos))
        return Button::Close;
    if (buttonRect(Button::Minimize).contains(pos))
        return Button::Minimize;
    if (buttonRect(Button::Maximize).contains(pos))
        return Button::Maximize;
    return Button::None;
}

void CustomTitleBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Background + a subtle divider against the content below.
    p.fillRect(rect(), palette().window());
    QColor divider = palette().mid().color();
    divider.setAlphaF(0.5f);
    p.fillRect(QRect(0, height() - 1, width(), 1), divider);

    // Traffic lights.
    const struct {
        Button button;
        QColor fill;
        QColor border;
        bool drawClose;
        bool drawMinus;
        bool drawPlus;
        bool drawRestore;
    } lights[] = {
        { Button::Close, QColor(0xFF, 0x5F, 0x57), QColor(0xE0, 0x44, 0x3E), true, false, false, false },
        { Button::Minimize, QColor(0xFE, 0xBC, 0x2E), QColor(0xD8, 0x9E, 0x24), false, true, false, false },
        { Button::Maximize, QColor(0x28, 0xC8, 0x40), QColor(0x1F, 0xA5, 0x30), false, false, true, true },
    };

    for (const auto &light : lights) {
        const QRect r = buttonRect(light.button);
        const bool hovered = hoverButton_ == light.button;
        QColor fill = light.fill;
        if (hovered)
            fill = fill.lighter(112);
        p.setPen(QPen(light.border, 1.0));
        p.setBrush(fill);
        p.drawEllipse(r);

        if (!hovered && pressButton_ != light.button)
            continue;

        // Glyph, macOS-style: shown only while hovered/pressed.
        p.setPen(QPen(QColor(0x50, 0x1A, 0x1A), 1.3, Qt::SolidLine, Qt::RoundCap));
        const int cx = r.center().x();
        const int cy = r.center().y();
        const int half = kGlyphInset;
        if (light.drawClose) {
            p.drawLine(QPointF(cx - half, cy - half), QPointF(cx + half, cy + half));
            p.drawLine(QPointF(cx - half, cy + half), QPointF(cx + half, cy - half));
        } else if (light.drawMinus) {
            p.drawLine(QPointF(cx - half, cy), QPointF(cx + half, cy));
        } else if (light.drawPlus) {
            p.drawLine(QPointF(cx - half, cy), QPointF(cx + half, cy));
            p.drawLine(QPointF(cx, cy - half), QPointF(cx, cy + half));
        } else if (light.drawRestore) {
            // Two overlapping squares for the "restore" glyph when maximized.
            p.drawRect(QRectF(cx - half, cy - half + 1, half + 1, half + 1));
            p.drawRect(QRectF(cx - 1, cy - half, half + 1, half + 1));
        }
    }

    // Centered, elided title.
    const QString title = window() ? window()->windowTitle() : QString();
    if (!title.isEmpty()) {
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() - 1.0);
        p.setFont(f);
        QColor textColor = palette().text().color();
        textColor.setAlphaF(0.75f);
        p.setPen(textColor);
        const QRect titleRect(kTitleMargin, 0, width() - kTitleMargin * 2, height());
        const QString elided =
                p.fontMetrics().elidedText(title, Qt::ElideRight, titleRect.width());
        p.drawText(titleRect, Qt::AlignCenter, elided);
    }
}

void CustomTitleBar::mousePressEvent(QMouseEvent *event)
{
    const Button button = buttonAt(event->pos());
    if (button != Button::None) {
        pressButton_ = button;
        update();
        activateButton(button);
        return;
    }

    // Manual-drag fallback for platforms without the WM_NCHITTEST path
    // (Windows uses HTCAPTION and never delivers these events).
    if (maximized_ || event->button() != Qt::LeftButton)
        return;
    QWindow *w = window() ? window()->windowHandle() : nullptr;
    if (!w)
        return;
    dragOffset_ = w->position() - event->globalPosition().toPoint();
    dragging_ = true;
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    const Button hovered = buttonAt(event->pos());
    if (hovered != hoverButton_) {
        hoverButton_ = hovered;
        update();
    }

    if (!dragging_)
        return;
    QWindow *w = window() ? window()->windowHandle() : nullptr;
    if (w)
        w->setPosition(event->globalPosition().toPoint() + dragOffset_);
}

void CustomTitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Non-Windows fallback: Windows handles double-click-maximize natively via
    // the HTCAPTION hit test.
    if (buttonAt(event->pos()) != Button::None)
        return;
    QWindow *w = window() ? window()->windowHandle() : nullptr;
    if (w)
        w->setWindowState(maximized_ ? Qt::WindowNoState : Qt::WindowMaximized);
}

void CustomTitleBar::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    hoverButton_ = Button::None;
    pressButton_ = Button::None;
    dragging_ = false;
    update();
}

void CustomTitleBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        updateMaximized();
}

void CustomTitleBar::activateButton(Button button)
{
    QWidget *w = window();
    if (!w)
        return;
    switch (button) {
    case Button::Close:
        w->close();
        break;
    case Button::Minimize:
        w->showMinimized();
        break;
    case Button::Maximize:
        if (maximized_)
            w->showNormal();
        else
            w->showMaximized();
        break;
    case Button::None:
        break;
    }
}

void CustomTitleBar::updateMaximized()
{
    const bool maximized = window() && window()->isMaximized();
    if (maximized == maximized_)
        return;
    maximized_ = maximized;
    update();
}

} // namespace UI
} // namespace Acheron

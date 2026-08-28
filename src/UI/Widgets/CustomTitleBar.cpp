#include "CustomTitleBar.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QEasingCurve>
#include <QVariantAnimation>
#include <QWindow>

namespace Acheron {
namespace UI {

namespace {
constexpr int kGlyphInset = 3; // glyph length inside the 12px button
constexpr int kHoverDurationMs = 130;
constexpr qreal kHoverGrow = 0.10; // diameter grows up to 10% on hover

QColor lerpColor(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}
} // namespace

CustomTitleBar::CustomTitleBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(kHeight);
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
}

void CustomTitleBar::setMenuButtonVisible(bool visible)
{
    if (menuButtonVisible_ == visible)
        return;
    menuButtonVisible_ = visible;
    if (!visible && hoverButton_ == Button::Menu)
        setHovered(Button::None);
    update();
}

QRect CustomTitleBar::buttonRect(Button button) const
{
    switch (button) {
    case Button::Close:
    case Button::Minimize:
    case Button::Maximize: {
        // Right-aligned row with close at the far corner (Windows placement,
        // macOS look): [min] [max] [close].
        const int index = button == Button::Close ? 0
                          : button == Button::Maximize ? 1
                                                       : 2;
        const int x = width() - kRightMargin - kButtonDiameter
                      - index * (kButtonDiameter + kButtonSpacing);
        const int y = (height() - kButtonDiameter) / 2;
        return QRect(x, y, kButtonDiameter, kButtonDiameter);
    }
    case Button::Menu:
        return QRect(kLeftMargin, 0, kMenuButtonWidth, height());
    case Button::None:
        break;
    }
    return {};
}

CustomTitleBar::Button CustomTitleBar::buttonAt(const QPoint &pos) const
{
    if (menuButtonVisible_ && buttonRect(Button::Menu).contains(pos))
        return Button::Menu;
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

    // Traffic lights (right side). Idle shades are the muted macOS colors;
    // hovering brightens them while the disc grows, driven by the hover
    // progress animation.
    drawTrafficLight(p, Button::Minimize, QColor(0xC8, 0x9A, 0x2A), QColor(0xFE, 0xBC, 0x2E),
                     QColor(0x66, 0x4A, 0x0D), false, true, false, false);
    drawTrafficLight(p, Button::Maximize, QColor(0x22, 0x9A, 0x34), QColor(0x28, 0xC8, 0x40),
                     QColor(0x0D, 0x4F, 0x18), false, false, true, true);
    drawTrafficLight(p, Button::Close, QColor(0xC9, 0x4B, 0x46), QColor(0xFF, 0x5F, 0x57),
                     QColor(0x66, 0x1C, 0x1A), true, false, false, false);

    drawMenuButton(p);

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

void CustomTitleBar::drawTrafficLight(QPainter &p, Button button, const QColor &idle,
                                      const QColor &hover, const QColor &glyphColor,
                                      bool drawClose, bool drawMinus, bool drawPlus,
                                      bool drawRestore)
{
    const qreal progress = hoverStates_.value(button).progress;
    const bool pressed = pressButton_ == button;
    const qreal bright = pressed ? 1.0 : progress;

    const QRect base = buttonRect(button);
    const qreal scale = 1.0 + kHoverGrow * progress;
    const int d = qRound(kButtonDiameter * scale);
    const QRectF disc(base.center().x() - d / 2.0, base.center().y() - d / 2.0, d, d);

    const QColor fill = lerpColor(idle, hover, bright);
    const QColor rim = pressed ? fill.darker(125) : fill.darker(112);
    p.setPen(QPen(rim, 1.0));
    p.setBrush(fill);
    p.drawEllipse(disc);

    // The glyph fades in with the hover animation (macOS behavior).
    const qreal glyphAlpha = pressed ? 1.0 : progress;
    if (glyphAlpha <= 0.01)
        return;

    p.setPen(QPen(glyphColor, 1.3, Qt::SolidLine, Qt::RoundCap));
    p.setOpacity(glyphAlpha);
    const qreal cx = disc.center().x();
    const qreal cy = disc.center().y();
    const qreal half = kGlyphInset;
    if (drawClose) {
        p.drawLine(QPointF(cx - half, cy - half), QPointF(cx + half, cy + half));
        p.drawLine(QPointF(cx - half, cy + half), QPointF(cx + half, cy - half));
    } else if (drawMinus) {
        p.drawLine(QPointF(cx - half, cy), QPointF(cx + half, cy));
    } else if (drawPlus) {
        p.drawLine(QPointF(cx - half, cy), QPointF(cx + half, cy));
        p.drawLine(QPointF(cx, cy - half), QPointF(cx, cy + half));
    } else if (drawRestore) {
        // Two overlapping squares for the "restore" glyph when maximized.
        p.drawRect(QRectF(cx - half, cy - half + 1, half + 1, half + 1));
        p.drawRect(QRectF(cx - 1, cy - half, half + 1, half + 1));
    }
    p.setOpacity(1.0);
}

void CustomTitleBar::drawMenuButton(QPainter &p)
{
    if (!menuButtonVisible_)
        return;
    const qreal progress = hoverStates_.value(Button::Menu).progress;
    const QRect r = buttonRect(Button::Menu);
    QColor line = palette().text().color();
    line.setAlphaF(0.40f + 0.35f * progress);
    p.setPen(QPen(line, 1.4, Qt::SolidLine, Qt::RoundCap));
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    for (int i = -1; i <= 1; ++i)
        p.drawLine(QPointF(cx - 5, cy + i * 3.0), QPointF(cx + 5, cy + i * 3.0));
}

void CustomTitleBar::mousePressEvent(QMouseEvent *event)
{
    const Button button = buttonAt(event->pos());
    if (button != Button::None) {
        pressButton_ = button;
        update();
        if (button == Button::Menu)
            emit menuRequested(mapToGlobal(buttonRect(Button::Menu).bottomLeft()));
        else
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

void CustomTitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (pressButton_ != Button::None) {
        pressButton_ = Button::None;
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

void CustomTitleBar::mouseMoveEvent(QMouseEvent *event)
{
    const Button hovered = buttonAt(event->pos());
    setHovered(hovered);
    setCursor(hovered == Button::None ? Qt::ArrowCursor : Qt::PointingHandCursor);

    if (!dragging_)
        return;
    QWindow *w = window() ? window()->windowHandle() : nullptr;
    if (w)
        w->setPosition(event->globalPosition().toPoint() + dragOffset_);
}

void CustomTitleBar::setHovered(Button button)
{
    if (button == hoverButton_)
        return;
    const Button previous = hoverButton_;
    hoverButton_ = button;
    if (previous != Button::None)
        animateHover(previous, 0.0);
    if (button != Button::None)
        animateHover(button, 1.0);
    update();
}

void CustomTitleBar::animateHover(Button button, qreal target)
{
    HoverState &state = hoverStates_[button];
    if (!state.animation) {
        state.animation = new QVariantAnimation(this);
        state.animation->setDuration(kHoverDurationMs);
        state.animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(state.animation, &QVariantAnimation::valueChanged, this,
                [this, button](const QVariant &value) {
                    hoverStates_[button].progress = value.toReal();
                    update();
                });
    }
    QVariantAnimation *anim = state.animation;
    anim->stop();
    anim->setStartValue(state.progress);
    anim->setEndValue(target);
    anim->start();
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
    setHovered(Button::None);
    pressButton_ = Button::None;
    dragging_ = false;
    setCursor(Qt::ArrowCursor);
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
    case Button::Menu:
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

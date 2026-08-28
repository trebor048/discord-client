#pragma once

#include <QWidget>
#include <QPoint>
#include <QHash>

class QVariantAnimation;

namespace Acheron {
namespace UI {

/// macOS-style custom title bar: three traffic-light buttons on the right
/// (minimize / maximize / close, close at the corner), an optional menu
/// (hamburger) button on the left, and a centered, elided window title.
///
/// Hovering a button smoothly grows it and brightens its shade (macOS
/// traffic-light behavior); leaving animates it back. On Windows the parent
/// top-level window drives dragging, double-click maximize and edge resizing
/// through WM_NCHITTEST (returning HTCAPTION / HT* resize codes), so this
/// widget only owns the painting and the button clicks; a manual-drag
/// fallback keeps it functional on other platforms.
class CustomTitleBar : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kHeight = 32;
    static constexpr int kButtonDiameter = 12;
    static constexpr int kButtonSpacing = 8;
    static constexpr int kRightMargin = 12;
    static constexpr int kLeftMargin = 8;       // hamburger inset
    static constexpr int kMenuButtonWidth = 26; // hamburger hit area
    static constexpr int kTitleMargin = 88;     // keep clear of both sides

    enum class Button { None, Menu, Close, Minimize, Maximize };

    explicit CustomTitleBar(QWidget *parent = nullptr);

    void setMenuButtonVisible(bool visible);

    [[nodiscard]] Button buttonAt(const QPoint &pos) const;
    [[nodiscard]] QRect buttonRect(Button button) const;
    [[nodiscard]] bool isMaximized() const { return maximized_; }

signals:
    /// Emitted when the hamburger menu button is clicked (with its global
    /// position); the owning window shows its menus there.
    void menuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void activateButton(Button button);
    void updateMaximized();
    void setHovered(Button button);
    void animateHover(Button button, qreal target);
    void drawTrafficLight(QPainter &p, Button button, const QColor &idle,
                          const QColor &hover, const QColor &glyph, bool drawClose,
                          bool drawMinus, bool drawPlus, bool drawRestore);
    void drawMenuButton(QPainter &p);

    Button hoverButton_ = Button::None;
    Button pressButton_ = Button::None;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool maximized_ = false;
    bool menuButtonVisible_ = false;

    struct HoverState
    {
        qreal progress = 0.0;
        QVariantAnimation *animation = nullptr;
    };
    QHash<Button, HoverState> hoverStates_;
};

} // namespace UI
} // namespace Acheron

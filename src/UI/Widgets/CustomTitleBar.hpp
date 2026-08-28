#pragma once

#include <QWidget>
#include <QPoint>

namespace Acheron {
namespace UI {

/// macOS-style custom title bar: three traffic-light buttons on the left
/// (close / minimize / maximize) and a centered, elided window title.
///
/// On Windows the parent top-level window drives dragging, double-click
/// maximize and edge resizing through WM_NCHITTEST (returning HTCAPTION /
/// HT* resize codes), so this widget only owns the painting and the button
/// clicks; a manual-drag fallback keeps it functional on other platforms
/// where the native event path is absent.
class CustomTitleBar : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kHeight = 32;
    static constexpr int kButtonDiameter = 12;
    static constexpr int kButtonSpacing = 8;
    static constexpr int kLeftMargin = 12;
    static constexpr int kTitleMargin = 96; // keep clear of the buttons

    enum class Button { None, Close, Minimize, Maximize };

    explicit CustomTitleBar(QWidget *parent = nullptr);

    [[nodiscard]] Button buttonAt(const QPoint &pos) const;
    [[nodiscard]] QRect buttonRect(Button button) const;
    [[nodiscard]] bool isMaximized() const { return maximized_; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void activateButton(Button button);
    void updateMaximized();

    Button hoverButton_ = Button::None;
    Button pressButton_ = Button::None;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool maximized_ = false;
};

} // namespace UI
} // namespace Acheron

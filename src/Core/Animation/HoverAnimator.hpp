#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPointer>

class QWidget;
class QAbstractAnimation;

namespace Acheron {
namespace Core {

/// App-wide animated hover via an overlay "wash" layer.
///
/// Qt stylesheets have no transitions, so a plain `:hover` rule snaps. Instead
/// of rewriting hundreds of stylesheet rules, this filter watches hover
/// events on interactive widgets and fades a translucent color wash in/out on
/// top of the widget, which visually smooths the hover without touching the
/// widget's own stylesheet. The wash is a child widget with
/// WA_TransparentForMouseEvents, so it never intercepts clicks.
///
/// Coverage: buttons, tool buttons, checkboxes, combo boxes, spin boxes,
/// sliders, tab bars, and rows of item views (list/tree/table viewports).
/// Respects AnimationConfig: with reduce-motion enabled no washes are created.
class HoverAnimator : public QObject
{
    Q_OBJECT
public:
    static HoverAnimator &instance();

    /// Install the filter on the application (idempotent).
    void install();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit HoverAnimator(QObject *parent = nullptr);

    struct WashState
    {
        QPointer<QWidget> overlay;   // the wash child widget
        QPointer<QAbstractAnimation> anim;
        QPointer<QWidget> target;
        bool isItemView = false;
        QRect itemRect;
        QPoint lastHoverPos;
    };

    bool eventFilterForWidget(QWidget *w, QEvent *event);
    void onHoverEnter(QWidget *w);
    void onHoverMove(QWidget *w, const QPoint &pos);
    void onHoverLeave(QWidget *w);
    void onPress(QWidget *w, bool pressed);
    void removeState(QWidget *w);

    QHash<QWidget *, WashState> states_;
    bool installed_ = false;
};

} // namespace Core
} // namespace Acheron

#pragma once

#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace Acheron {
namespace UI {

class MemberListView;

/// Slide-out container for the member list: a frameless panel pinned to the
/// right edge of the main window. Collapsed it shows a narrow avatar strip;
/// hovering expands it over the chat; leaving collapses it back. Only used
/// in AppearanceConfig::SlideOut mode.
class MemberListOverlay : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kCollapsedWidth = 56;
    static constexpr int kExpandedWidth = 240;

    explicit MemberListOverlay(MemberListView *view, QWidget *parent = nullptr);

    bool expanded() const { return expanded_; }
    MemberListView *view() const { return view_; }

public slots:
    void expand();
    void collapse();

protected:
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void collapseNow();

private:
    void animateWidth(int targetWidth);
    QRect overlayRect(int width) const;
    void applyIconsOnly(bool iconsOnly);
    void reposition();

    MemberListView *view_ = nullptr;
    bool expanded_ = false;
    QTimer *collapseTimer_ = nullptr;
    QPropertyAnimation *widthAnimation_ = nullptr;
};

} // namespace UI
} // namespace Acheron

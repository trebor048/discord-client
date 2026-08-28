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
/// in AppearanceConfig::SlideOut mode. Expand and collapse are single motions:
/// the width animation drives the content opacity, so names never pop in after
/// the panel has finished sliding.
class MemberListOverlay : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kCollapsedWidth = 56;
    static constexpr int kExpandedWidth = 240;

    explicit MemberListOverlay(MemberListView *view, QWidget *parent = nullptr);
    ~MemberListOverlay() override;

    bool expanded() const { return expanded_; }
    MemberListView *view() const { return view_; }

    /// Content opacity for a given panel width: 0 at the collapsed strip,
    /// 1 at full width, monotonic between (drives the one-motion slide).
    static qreal contentOpacityForWidth(int width);

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
    void setContentOpacityForWidth(int width);
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

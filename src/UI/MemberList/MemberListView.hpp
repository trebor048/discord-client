#pragma once

#include <QListView>
#include <QShowEvent>

namespace Acheron {
namespace UI {

class MemberListView : public QListView
{
    Q_OBJECT
public:
    explicit MemberListView(QWidget *parent = nullptr);

    void setOverlayMode(bool overlayMode) { overlayMode_ = overlayMode; }

signals:
    void visibleRangeChanged(int firstVisible, int lastVisible);

protected:
    void scrollContentsBy(int dx, int dy) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    QSize sizeHint() const override { return { 240, 0 }; }

private:
    void emitVisibleRange();

    int lastFirstVisible = -1;
    int lastLastVisible = -1;
    bool hasBeenShown = false;
    bool overlayMode_ = false;
};

} // namespace UI
} // namespace Acheron

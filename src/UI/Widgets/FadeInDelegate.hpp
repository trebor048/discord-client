#pragma once

#include <QHash>
#include <QPointer>
#include <QSet>
#include <QStyledItemDelegate>

#include <functional>

class QAbstractItemView;
class QVariantAnimation;

namespace Acheron {
namespace UI {

/// A delegate that fades rows in/out when they are added or removed from a view.
///
/// Rows tracked by this delegate are painted with a per-row opacity. Rows
/// fading in animate 0 -> 1, rows fading out animate 1 -> 0 (the caller
/// removes the row once the fade completes). Untracked rows paint normally, so
/// the delegate is safe to install on any list view.
class FadeInDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit FadeInDelegate(QObject *parent = nullptr);

    /// Fade the given inclusive row range in (0-indexed). Rows not currently
    /// tracked are seeded at opacity 0 and animated to 1.
    void fadeInRows(int start, int end);

    /// Fade every existing row in at once (used after a full repopulate).
    void fadeInAll(int rowCount);

    /// Fade a single row out and invoke `done` (which should remove the row
    /// from the model) when it is fully transparent.
    void fadeOutRow(int row, std::function<void()> done);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

private:
    void onTick(qreal value);
    void ensureAnimation();

    QHash<int, qreal> rowOpacity;      // row -> current opacity (0..1)
    QSet<int> fadingOut;               // rows currently animating 1 -> 0
    QHash<int, std::function<void()>> pendingDone; // fade-out completion callbacks
    QPointer<QVariantAnimation> animation;
};

} // namespace UI
} // namespace Acheron

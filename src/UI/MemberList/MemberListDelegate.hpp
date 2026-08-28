#pragma once

#include <QStyledItemDelegate>
#include <QVariantMap>
#include <QCache>
#include <QPixmap>

namespace Acheron {
namespace UI {

class MemberListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
    Q_PROPERTY(qreal contentOpacity READ contentOpacity WRITE setContentOpacity)
public:
    explicit MemberListDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    void setIconsOnly(bool iconsOnly) { iconsOnly_ = iconsOnly; }
    bool iconsOnly() const { return iconsOnly_; }

    qreal contentOpacity() const { return contentOpacity_; }
    void setContentOpacity(qreal opacity) { contentOpacity_ = opacity; }

private:
    void paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                    const QModelIndex &index) const;
    void paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                     const QModelIndex &index) const;
    void drawPresenceIcon(QPainter *painter, const QVariantMap &presence,
                          const QRect &rect) const;
    void paintPlaceholder(QPainter *painter, const QStyleOptionViewItem &option) const;

    // Smooth-scales a source avatar to a square of `size` px and caches the
    // result. Avatars come from ImageManager's cache (stable cacheKey per
    // user), so during a scroll every visible row repaints each frame and
    // would otherwise re-run the smooth transform for its avatar on every
    // frame.
    [[nodiscard]] QPixmap scaledAvatar(const QPixmap &source, int size) const;

    bool iconsOnly_ = false;
    qreal contentOpacity_ = 1.0;
    mutable QCache<QPair<qint64, QSize>, QPixmap> m_scaledAvatars;
};

} // namespace UI
} // namespace Acheron

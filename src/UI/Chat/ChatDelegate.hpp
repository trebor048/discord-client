#pragma once

#include <QCache>
#include <QStyledItemDelegate>

namespace Acheron {
namespace Core {
class ImageManager;
}
namespace UI {

// Key for the scaled-pixmap cache. QPixmap::cacheKey() identifies the
// underlying image data, so a stable source pixmap always yields the same key
// and a replaced pixmap (new image data) naturally misses.
struct ScaledPixmapKey
{
    qint64 sourceId = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const ScaledPixmapKey &a, const ScaledPixmapKey &b)
    {
        return a.sourceId == b.sourceId && a.width == b.width && a.height == b.height;
    }
};

inline size_t qHash(const ScaledPixmapKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.sourceId, key.width, key.height);
}

// Key for the blurred-pixmap cache. The radius is part of the identity so a
// different blur radius can never hit another radius's cached result.
struct BlurKey
{
    qint64 sourceId = 0;
    int radius = 0;

    friend bool operator==(const BlurKey &a, const BlurKey &b)
    {
        return a.sourceId == b.sourceId && a.radius == b.radius;
    }
};

inline size_t qHash(const BlurKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.sourceId, key.radius);
}

class ChatDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChatDelegate(Core::ImageManager *imageManager, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), imageManager(imageManager)
    {
        // Bound the caches by total pixels (~16 MiB of RGBA per cache).
        scaledCache.setMaxCost(4 * 1024 * 1024);
        blurredCache.setMaxCost(4 * 1024 * 1024);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    // Smooth-scale `source` to `target`, cached so per-paint rescaling on the
    // UI thread only happens once per (pixmap, size) pair.
    QPixmap scaledCached(const QPixmap &source, const QSize &target) const;
    // Blur `source` once and reuse; QGraphicsScene blur is very expensive to
    // redo on every paint of a spoiler attachment.
    QPixmap blurredCached(const QPixmap &source, int radius) const;

    Core::ImageManager *imageManager;
    mutable QCache<ScaledPixmapKey, QPixmap> scaledCache;
    mutable QCache<BlurKey, QPixmap> blurredCache;
};
} // namespace UI
} // namespace Acheron

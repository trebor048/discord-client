#pragma once

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

#include <QtGlobal>

namespace Acheron {
namespace Core {
namespace Theme {

/// Discord-style avatar corner radius: a fixed fraction of the avatar size,
/// so small avatars stay soft and large ones never collapse into circles.
/// 25% matches the familiar "squircle" look used for user avatars; the
/// server rail uses its own 30% variant (see ServerRailDelegate).
inline int avatarRadius(int size) noexcept
{
    return qMax(4, qRound(size * 0.25f));
}

/// Renders `src` clipped to a rounded square of the given size. Used where
/// avatars are shown through plain widgets (e.g. the MePanel's QLabel),
/// which do not clip their pixmaps to the widget's border radius.
inline QPixmap roundedAvatarPixmap(const QPixmap &src, int size, int radius = -1)
{
    if (src.isNull())
        return src;

    QPixmap out(size, size);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int r = radius < 0 ? avatarRadius(size) : radius;
    QPainterPath clip;
    clip.addRoundedRect(0, 0, size, size, r, r);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, size, size, src);
    return out;
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

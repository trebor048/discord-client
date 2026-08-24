#include "MemberListDelegate.hpp"

#include <QPainter>
#include <QPainterPath>

#include "MemberListModel.hpp"
#include "Core/Appearance/AppearanceConfig.hpp"
#include "Core/MemberListManager.hpp"
#include "Core/Theme/Icons.hpp"

constexpr static int kGroupHeight = 22;
constexpr static int kMemberHeight = 28;
constexpr static int kAvatarSize = 20;
constexpr static int kAvatarRadius = 4;
constexpr static int kHorizontalPadding = 8;
constexpr static int kAvatarTextSpacing = 8;

constexpr static int kPresenceIconSize = 11;
constexpr static int kRoleIconSize = 14;
constexpr static int kIconSpacing = 5;
constexpr static int kRoleBadgeSize = 8;
constexpr static int kGroupFontPx = 10;
constexpr static int kMemberFontPx = 12;

namespace Acheron {
namespace UI {

float memberScale()
{
    return Core::Appearance::AppearanceConfig::instance().memberCardScale();
}

MemberListDelegate::MemberListDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void MemberListDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                               const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    int itemType = index.data(MemberListModel::ItemTypeRole).toInt();

    if (itemType == static_cast<int>(Core::MemberListItem::Type::Group))
        paintGroup(painter, option, index);
    else if (itemType == static_cast<int>(Core::MemberListItem::Type::Member))
        paintMember(painter, option, index);
    else
        paintPlaceholder(painter, option);

    painter->restore();
}

QSize MemberListDelegate::sizeHint(const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    int itemType = index.data(MemberListModel::ItemTypeRole).toInt();

    if (itemType == static_cast<int>(Core::MemberListItem::Type::Group))
        return QSize(option.rect.width(), Core::Appearance::AppearanceConfig::scaledInt(kGroupHeight, memberScale()));

    return QSize(option.rect.width(),
                 Core::Appearance::AppearanceConfig::scaledInt(kMemberHeight, memberScale()));
}

void MemberListDelegate::paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    const QString groupName = index.data(MemberListModel::GroupNameRole).toString();
    const int groupCount = index.data(MemberListModel::GroupCountRole).toInt();
    const float scale = memberScale();

    // separator except for the first (drawn in both modes)
    if (index.row() > 0) {
        QColor sepColor = option.palette.mid().color();
        sepColor.setAlpha(60);
        painter->setPen(QPen(sepColor, 1));
        painter->drawLine(option.rect.left() + Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale),
                          option.rect.top(),
                          option.rect.right() - Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale),
                          option.rect.top());
    }

    if (iconsOnly_)
        return;

    QString text = groupName.toUpper() + QString::fromUtf8(" \u2014 ") + QString::number(groupCount);

    QFont font = option.font;
    font.setPixelSize(Core::Appearance::AppearanceConfig::scaledInt(kGroupFontPx, scale));
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    painter->setFont(font);

    painter->setPen(option.palette.color(QPalette::Disabled, QPalette::Text));

    QRect textRect = option.rect.adjusted(Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale), 0,
                                          -Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale), 0);
    textRect.setTop(textRect.top() + 6);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}

void MemberListDelegate::paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    const float scale = memberScale();
    const int avatarSize = Core::Appearance::AppearanceConfig::scaledInt(kAvatarSize, scale);
    const int avatarRadius = Core::Appearance::AppearanceConfig::scaledInt(kAvatarRadius, scale);
    const int horizontalPadding = Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale);
    const int avatarTextSpacing = Core::Appearance::AppearanceConfig::scaledInt(kAvatarTextSpacing, scale);

    if (iconsOnly_) {
        const int stripAvatar = qMax(avatarSize, option.rect.height() - 8);
        const int x = option.rect.left() + (option.rect.width() - stripAvatar) / 2;
        const int centerY = option.rect.top() + (option.rect.height() - stripAvatar) / 2;
        QRect avatarRect(x, centerY, stripAvatar, stripAvatar);

        QPixmap avatar = index.data(MemberListModel::AvatarRole).value<QPixmap>();
        if (!avatar.isNull()) {
            QPainterPath clipPath;
            clipPath.addRoundedRect(avatarRect, avatarRadius, avatarRadius);
            painter->save();
            painter->setClipPath(clipPath);
            avatar = avatar.scaled(stripAvatar, stripAvatar,
                                   Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            painter->drawPixmap(avatarRect, avatar);
            painter->restore();
        } else {
            QColor defaultBg = option.palette.mid().color();
            defaultBg.setAlpha(100);
            painter->setBrush(defaultBg);
            painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(avatarRect, avatarRadius, avatarRadius);
        }
        return;
    }

    if (option.state & QStyle::State_MouseOver) {
        QColor hoverColor = option.palette.highlight().color();
        hoverColor.setAlpha(30);
        painter->fillRect(option.rect.adjusted(horizontalPadding / 2, 1,
                                               -horizontalPadding / 2, -1),
                          hoverColor);
    }

    int x = option.rect.left() + horizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - avatarSize) / 2;

    QPixmap avatar = index.data(MemberListModel::AvatarRole).value<QPixmap>();
    QRect avatarRect(x, centerY, avatarSize, avatarSize);

    if (!avatar.isNull()) {
        QPainterPath clipPath;
        clipPath.addRoundedRect(avatarRect, avatarRadius, avatarRadius);
        painter->save();
        painter->setClipPath(clipPath);
        if (avatar.width() != avatarSize || avatar.height() != avatarSize)
            avatar = avatar.scaled(avatarSize, avatarSize,
                                   Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter->drawPixmap(avatarRect, avatar);
        painter->restore();
    } else {
        QColor defaultBg = option.palette.mid().color();
        defaultBg.setAlpha(100);
        painter->setBrush(defaultBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(avatarRect, avatarRadius, avatarRadius);
    }

    x += avatarSize + avatarTextSpacing;

    // Presence icon (device glyph colored by status) + role icon are drawn
    // right-aligned. Sizes scale with the member scale.
    const int presenceIconSize = Core::Appearance::AppearanceConfig::scaledInt(kPresenceIconSize, scale);
    const int roleIconSize = Core::Appearance::AppearanceConfig::scaledInt(kRoleIconSize, scale);
    const int iconSpacing = Core::Appearance::AppearanceConfig::scaledInt(kIconSpacing, scale);
    const QVariantMap presenceMap = index.data(MemberListModel::PresenceRole).toMap();
    const bool hasPresence = presenceMap.contains("status");
    const QPixmap roleIconPm = index.data(MemberListModel::RoleIconRole).value<QPixmap>();
    const bool hasRoleIcon = !roleIconPm.isNull();
    int rightIconsWidth = 0;
    if (hasPresence)
        rightIconsWidth += presenceIconSize + iconSpacing;
    if (hasRoleIcon)
        rightIconsWidth += roleIconSize + iconSpacing;

    int textWidth = option.rect.right() - x - horizontalPadding - rightIconsWidth;
    const int nameWidth = qMax(0, textWidth);
    QRect nameRect(x, option.rect.top(), nameWidth, option.rect.height());

    QString displayName = index.data(MemberListModel::UsernameRole).toString();
    QColor roleColor = index.data(MemberListModel::RoleColorRole).value<QColor>();

    QFont font = option.font;
    font.setPixelSize(Core::Appearance::AppearanceConfig::scaledInt(kMemberFontPx, scale));
    font.setWeight(QFont::Medium);
    painter->setFont(font);

    QColor nameColor;
    if (roleColor.isValid())
        nameColor = roleColor;
    else
        nameColor = option.palette.color(QPalette::Text);

    painter->setPen(nameColor);

    QFontMetrics fm(font);
    QString elidedName = fm.elidedText(displayName, Qt::ElideRight, nameWidth);
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

    const int iconCenterY = option.rect.top() + option.rect.height() / 2;
    int iconX = option.rect.right() - horizontalPadding;

    if (hasRoleIcon) {
        iconX -= roleIconSize;
        painter->drawPixmap(QRect(iconX, iconCenterY - roleIconSize / 2, roleIconSize, roleIconSize),
                            roleIconPm);
        iconX -= iconSpacing;
    }

    if (hasPresence) {
        iconX -= presenceIconSize;
        drawPresenceIcon(painter, presenceMap,
                         QRect(iconX, iconCenterY - presenceIconSize / 2,
                               presenceIconSize, presenceIconSize));
    }

    // Role badge: small filled dot with the member's highest role color,
    // overlapping the avatar's bottom-right corner.
    const QColor roleBadgeColor = index.data(MemberListModel::RoleBadgeColorRole).value<QColor>();
    if (roleBadgeColor.isValid()) {
        const int roleBadgeSize = Core::Appearance::AppearanceConfig::scaledInt(kRoleBadgeSize, scale);
        const QRect badgeRect(avatarRect.right() - roleBadgeSize / 2,
                              avatarRect.bottom() - roleBadgeSize / 2,
                              roleBadgeSize, roleBadgeSize);
        painter->setPen(Qt::NoPen);
        painter->setBrush(roleBadgeColor);
        painter->drawEllipse(badgeRect);
    }
}

void MemberListDelegate::drawPresenceIcon(QPainter *painter, const QVariantMap &presence,
                                          const QRect &rect) const
{
    const QString status = presence.value("status").toString();
    const QString device = presence.value("device").toString();
    const QString deviceStatus = presence.value("deviceStatus").toString();

    // No active device -> a plain status dot.
    if (device.isEmpty()) {
        const QString colorStatus = deviceStatus.isEmpty() ? status : deviceStatus;
        QColor color;
        if (colorStatus == QLatin1String("online"))
            color = QColor(0x23, 0xA5, 0x5A);
        else if (colorStatus == QLatin1String("idle"))
            color = QColor(0xF0, 0xB2, 0x32);
        else if (colorStatus == QLatin1String("dnd"))
            color = QColor(0xF2, 0x3F, 0x43);
        else
            color = QColor(0x80, 0x84, 0x8E);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(rect);
        return;
    }

    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const QPixmap pm = Core::Theme::Icons::presencePixmap(status, device, deviceStatus,
                                                          rect.width(), dpr);
    if (!pm.isNull())
        painter->drawPixmap(rect, pm);
}

void MemberListDelegate::paintPlaceholder(QPainter *painter,
                                          const QStyleOptionViewItem &option) const
{
    const float scale = memberScale();
    const int avatarSize = Core::Appearance::AppearanceConfig::scaledInt(kAvatarSize, scale);
    const int avatarRadius = Core::Appearance::AppearanceConfig::scaledInt(kAvatarRadius, scale);
    const int horizontalPadding = Core::Appearance::AppearanceConfig::scaledInt(kHorizontalPadding, scale);
    const int avatarTextSpacing = Core::Appearance::AppearanceConfig::scaledInt(kAvatarTextSpacing, scale);

    QColor placeholderColor = option.palette.mid().color();
    placeholderColor.setAlpha(40);
    painter->setPen(Qt::NoPen);
    painter->setBrush(placeholderColor);

    int x = option.rect.left() + horizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - avatarSize) / 2;

    painter->drawRoundedRect(QRect(x, centerY, avatarSize, avatarSize),
                             avatarRadius, avatarRadius);

    if (iconsOnly_)
        return;

    x += avatarSize + avatarTextSpacing;
    int nameWidth = qMin(80, option.rect.right() - x - horizontalPadding);
    int nameHeight = 10;
    int nameY = option.rect.top() + (option.rect.height() - nameHeight) / 2;
    painter->drawRoundedRect(QRect(x, nameY, nameWidth, nameHeight), 3, 3);
}

} // namespace UI
} // namespace Acheron

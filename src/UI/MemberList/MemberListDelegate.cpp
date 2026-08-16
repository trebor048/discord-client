#include "MemberListDelegate.hpp"

#include <QPainter>
#include <QPainterPath>

#include "MemberListModel.hpp"
#include "Core/MemberListManager.hpp"
#include "Core/Theme/Icons.hpp"

constexpr static int GroupHeight = 22;
constexpr static int MemberHeight = 28;
constexpr static int AvatarSize = 20;
constexpr static int AvatarRadius = 4;
constexpr static int HorizontalPadding = 8;
constexpr static int AvatarTextSpacing = 8;

namespace Acheron {
namespace UI {

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
        return QSize(option.rect.width(), GroupHeight);

    return QSize(option.rect.width(), MemberHeight);
}

void MemberListDelegate::paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    QString groupName = index.data(MemberListModel::GroupNameRole).toString();
    int groupCount = index.data(MemberListModel::GroupCountRole).toInt();

    // separator except for the first
    if (index.row() > 0) {
        QColor sepColor = option.palette.mid().color();
        sepColor.setAlpha(60);
        painter->setPen(QPen(sepColor, 1));
        painter->drawLine(option.rect.left() + HorizontalPadding,
                          option.rect.top(),
                          option.rect.right() - HorizontalPadding,
                          option.rect.top());
    }

    QString text = groupName.toUpper() + QString::fromUtf8(" \u2014 ") + QString::number(groupCount);

    QFont font = option.font;
    font.setPixelSize(10);
    font.setWeight(QFont::DemiBold);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.3);
    painter->setFont(font);

    painter->setPen(option.palette.color(QPalette::Disabled, QPalette::Text));

    QRect textRect = option.rect.adjusted(HorizontalPadding, 0, -HorizontalPadding, 0);
    textRect.setTop(textRect.top() + 6);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
}

void MemberListDelegate::paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    if (option.state & QStyle::State_MouseOver) {
        QColor hoverColor = option.palette.highlight().color();
        hoverColor.setAlpha(30);
        painter->fillRect(option.rect.adjusted(HorizontalPadding / 2, 1,
                                               -HorizontalPadding / 2, -1),
                          hoverColor);
    }

    int x = option.rect.left() + HorizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - AvatarSize) / 2;

    QPixmap avatar = index.data(MemberListModel::AvatarRole).value<QPixmap>();
    QRect avatarRect(x, centerY, AvatarSize, AvatarSize);

    if (!avatar.isNull()) {
        QPainterPath clipPath;
        clipPath.addRoundedRect(avatarRect, AvatarRadius, AvatarRadius);
        painter->save();
        painter->setClipPath(clipPath);
        // Only scale if the avatar isn't already the target size
        if (avatar.width() != AvatarSize || avatar.height() != AvatarSize)
            avatar = avatar.scaled(AvatarSize, AvatarSize,
                                   Qt::KeepAspectRatioByExpanding,
                                   Qt::SmoothTransformation);
        painter->drawPixmap(avatarRect, avatar);
        painter->restore();
    } else {
        QColor defaultBg = option.palette.mid().color();
        defaultBg.setAlpha(100);
        painter->setBrush(defaultBg);
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(avatarRect, AvatarRadius, AvatarRadius);
    }

    x += AvatarSize + AvatarTextSpacing;

    // Presence icon (device glyph colored by status) + role icon (if the top
    // role has one) are drawn right-aligned.
    const int PresenceIconSize = 11;
    const int RoleIconSize = 14;
    const int IconSpacing = 5;
    const QVariantMap presenceMap = index.data(MemberListModel::PresenceRole).toMap();
    const bool hasPresence = presenceMap.contains("status");
    const QPixmap roleIconPm = index.data(MemberListModel::RoleIconRole).value<QPixmap>();
    const bool hasRoleIcon = !roleIconPm.isNull();
    int rightIconsWidth = 0;
    if (hasPresence)
        rightIconsWidth += PresenceIconSize + IconSpacing;
    if (hasRoleIcon)
        rightIconsWidth += RoleIconSize + IconSpacing;

    int textWidth = option.rect.right() - x - HorizontalPadding - rightIconsWidth;
    const int nameWidth = qMax(0, textWidth);
    QRect nameRect(x, option.rect.top(), nameWidth, option.rect.height());

    QString displayName = index.data(MemberListModel::UsernameRole).toString();
    QColor roleColor = index.data(MemberListModel::RoleColorRole).value<QColor>();

    QFont font = option.font;
    font.setPixelSize(12);
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
    int iconX = option.rect.right() - HorizontalPadding;

    if (hasRoleIcon) {
        iconX -= RoleIconSize;
        painter->drawPixmap(QRect(iconX, iconCenterY - RoleIconSize / 2, RoleIconSize, RoleIconSize),
                            roleIconPm);
        iconX -= IconSpacing;
    }

    if (hasPresence) {
        iconX -= PresenceIconSize;
        drawPresenceIcon(painter, presenceMap,
                         QRect(iconX, iconCenterY - PresenceIconSize / 2, PresenceIconSize, PresenceIconSize));
    }

    // Role badge: small filled dot with the member's highest role color,
    // overlapping the avatar's bottom-right corner. Skipped when no provider
    // is set or the color is invalid.
    const QColor roleBadgeColor = index.data(MemberListModel::RoleBadgeColorRole).value<QColor>();
    if (roleBadgeColor.isValid()) {
        constexpr int RoleBadgeSize = 8;
        const QRect badgeRect(avatarRect.right() - RoleBadgeSize / 2,
                              avatarRect.bottom() - RoleBadgeSize / 2,
                              RoleBadgeSize, RoleBadgeSize);
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
    QColor placeholderColor = option.palette.mid().color();
    placeholderColor.setAlpha(40);
    painter->setPen(Qt::NoPen);
    painter->setBrush(placeholderColor);

    int x = option.rect.left() + HorizontalPadding;
    int centerY = option.rect.top() + (option.rect.height() - AvatarSize) / 2;

    painter->drawRoundedRect(QRect(x, centerY, AvatarSize, AvatarSize),
                             AvatarRadius, AvatarRadius);

    x += AvatarSize + AvatarTextSpacing;
    int nameWidth = qMin(80, option.rect.right() - x - HorizontalPadding);
    int nameHeight = 10;
    int nameY = option.rect.top() + (option.rect.height() - nameHeight) / 2;
    painter->drawRoundedRect(QRect(x, nameY, nameWidth, nameHeight), 3, 3);
}

} // namespace UI
} // namespace Acheron

#include "MemberListModel.hpp"

#include "Discord/CdnUrls.hpp"

namespace Acheron {
namespace UI {

constexpr static QSize AvatarRequestSize = QSize(32, 32);

MemberListModel::MemberListModel(Core::ImageManager *imageManager, QObject *parent)
    : QAbstractListModel(parent), imageManager(imageManager)
{
    if (imageManager) {
        connect(imageManager, &Core::ImageManager::imageFetched, this,
                &MemberListModel::onImageFetched);
    }
}

void MemberListModel::setManager(Core::MemberListManager *newManager)
{
    beginResetModel();
    disconnectManager();
    manager = newManager;
    avatarTracker.clear();
    roleIconTracker.clear();
    connectManager();
    endResetModel();
}

int MemberListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !manager)
        return 0;

    return manager->totalItemCount();
}

QVariant MemberListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !manager)
        return {};

    int row = index.row();
    if (row < 0 || row >= manager->totalItemCount())
        return {};

    const auto *item = manager->itemAt(row);

    if (!item) {
        switch (role) {
        case ItemTypeRole:
            return static_cast<int>(Core::MemberListItem::Type::Placeholder);
        case LoadedRole:
            return false;
        default:
            return {};
        }
    }

    switch (role) {
    case ItemTypeRole:
        return static_cast<int>(item->type);
    case LoadedRole:
        return true;
    case UserIdRole:
        return item->type == Core::MemberListItem::Type::Member
                       ? QVariant::fromValue(static_cast<quint64>(item->userId))
                       : QVariant();
    case UsernameRole:
        return item->type == Core::MemberListItem::Type::Member
                       ? item->displayName
                       : QString();
    case AvatarRole: {
        if (item->type != Core::MemberListItem::Type::Member || !imageManager)
            return QVariant();

        if (!item->member.user.hasValue() || !item->member.user->avatar.hasValue())
            return QVariant();

        Core::Snowflake userId = item->userId;
        QString avatarHash = item->member.user->avatar.get();
        if (avatarHash.isEmpty())
            return QVariant();

        QUrl url = Discord::Cdn::userAvatar(userId, avatarHash, AvatarRequestSize.width());

        if (imageManager->isCached(url, AvatarRequestSize))
            return imageManager->get(url, AvatarRequestSize);

        imageManager->get(url, AvatarRequestSize);
        avatarTracker.track(url, index);

        return QVariant();
    }
    case RoleColorRole:
        return item->type == Core::MemberListItem::Type::Member
                       ? QVariant::fromValue(item->roleColor)
                       : QVariant();
    case RoleIconRole: {
        if (item->type != Core::MemberListItem::Type::Member || !imageManager
            || item->roleIconHash.isEmpty() || !item->roleIconRoleId.isValid())
            return QVariant();
        const QSize iconSize(16, 16);
        const QUrl url = Discord::Cdn::roleIcon(item->roleIconRoleId, item->roleIconHash, 32);
        // Mirror the avatar path: don't return the gray placeholder as if it were
        // a real icon; return nothing until the icon is fetched.
        if (imageManager->isCached(url, iconSize))
            return imageManager->get(url, iconSize);
        imageManager->get(url, iconSize);
        roleIconTracker.track(url, index);
        return QVariant();
    }
    case RoleBadgeColorRole: {
        if (item->type != Core::MemberListItem::Type::Member || !m_roleColorProvider || !manager)
            return QVariant();
        return QVariant::fromValue(
                m_roleColorProvider(item->userId, manager->currentGuildId()));
    }
    case PresenceRole: {
        if (item->type != Core::MemberListItem::Type::Member || !m_presenceProvider)
            return QVariant();
        const auto p = m_presenceProvider(item->userId);
        if (!p)
            return QVariant();
        // Pick the primary device (desktop > mobile > web), first non-offline.
        QString device;
        QString deviceStatus;
        const auto pick = [&device, &deviceStatus](const QString &d, const QString &s) {
            if (device.isEmpty() && !s.isEmpty() && s != QLatin1String("offline")) {
                device = d;
                deviceStatus = s;
            }
        };
        pick(QStringLiteral("desktop"), p->desktop);
        pick(QStringLiteral("mobile"), p->mobile);
        pick(QStringLiteral("web"), p->web);
        QVariantMap map;
        map["status"] = p->status;
        map["device"] = device;
        map["deviceStatus"] = deviceStatus.isEmpty() ? p->status : deviceStatus;
        return map;
    }
    case GroupNameRole:
        return item->type == Core::MemberListItem::Type::Group
                       ? item->groupName
                       : QString();
    case GroupCountRole:
        return item->type == Core::MemberListItem::Type::Group
                       ? item->groupCount
                       : 0;
    case GroupColorRole:
        return item->type == Core::MemberListItem::Type::Group
                       ? QVariant::fromValue(item->groupColor)
                       : QVariant();
    }

    return {};
}

void MemberListModel::setPresenceProvider(PresenceProvider provider)
{
    m_presenceProvider = std::move(provider);
    if (manager)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { PresenceRole });
}

void MemberListModel::setRoleColorProvider(RoleColorProvider provider)
{
    m_roleColorProvider = std::move(provider);
    if (manager)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { RoleBadgeColorRole });
}

void MemberListModel::notifyPresenceChanged(Core::Snowflake userId)
{
    if (!manager)
        return;
    for (int row = 0; row < manager->totalItemCount(); ++row) {
        const auto *item = manager->itemAt(row);
        if (item && item->type == Core::MemberListItem::Type::Member && item->userId == userId) {
            emit dataChanged(index(row, 0), index(row, 0), { PresenceRole });
            break;
        }
    }
}

void MemberListModel::onListAboutToReset()
{
    beginResetModel();
}

void MemberListModel::onListReset()
{
    avatarTracker.clear();
    roleIconTracker.clear();
    endResetModel();
}

void MemberListModel::onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap)
{
    Q_UNUSED(size);
    Q_UNUSED(pixmap);

    avatarTracker.notify(url, [this](const QModelIndex &index) {
        if (index.isValid())
            emit dataChanged(index, index);
    });
    roleIconTracker.notify(url, [this](const QModelIndex &index) {
        if (index.isValid())
            emit dataChanged(index, index);
    });
}

void MemberListModel::connectManager()
{
    if (!manager)
        return;

    connect(manager, &Core::MemberListManager::listAboutToReset,
            this, &MemberListModel::onListAboutToReset);
    connect(manager, &Core::MemberListManager::listReset,
            this, &MemberListModel::onListReset);
}

void MemberListModel::disconnectManager()
{
    if (!manager)
        return;

    disconnect(manager, nullptr, this, nullptr);
}

} // namespace UI
} // namespace Acheron

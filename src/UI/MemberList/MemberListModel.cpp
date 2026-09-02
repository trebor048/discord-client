#include "MemberListModel.hpp"

#include "Discord/CdnUrls.hpp"

namespace Acheron {
namespace UI {

namespace {
// Builds the PresenceRole payload (identical keys/values to the previous
// per-paint code); now computed once per presence change and cached.
QVariantMap buildPresenceMap(const Core::ClientInstance::UserPresence &p)
{
    // Pick the primary device (desktop > mobile > web), first non-offline.
    QString device;
    QString deviceStatus;
    const auto pick = [&device, &deviceStatus](const QString &d, const QString &s) {
        if (device.isEmpty() && !s.isEmpty() && s != QLatin1String("offline")) {
            device = d;
            deviceStatus = s;
        }
    };
    pick(QStringLiteral("desktop"), p.desktop);
    pick(QStringLiteral("mobile"), p.mobile);
    pick(QStringLiteral("web"), p.web);
    QVariantMap map;
    map["status"] = p.status;
    map["device"] = device;
    map["deviceStatus"] = deviceStatus.isEmpty() ? p.status : deviceStatus;
    return map;
}
} // namespace

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
    rebuildPresenceCache();
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

        // Use the tracker helper: it only registers the URL when get() returns
        // a usable (placeholder) pixmap, so unresolvable/failed URLs don't leak
        // a pending entry that can never be notified (imageFailed never emits
        // imageFetched, and ImageManager blocks re-requests for failed keys).
        return avatarTracker.fetch(imageManager, url, AvatarRequestSize, index);
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
        return roleIconTracker.fetch(imageManager, url, iconSize, index);
    }
    case RoleBadgeColorRole: {
        if (item->type != Core::MemberListItem::Type::Member || !m_roleColorProvider || !manager)
            return QVariant();
        return QVariant::fromValue(
                m_roleColorProvider(item->userId, manager->currentGuildId()));
    }
    case PresenceRole: {
        if (item->type != Core::MemberListItem::Type::Member)
            return QVariant();
        const auto it = m_presenceCache.constFind(item->userId);
        if (it == m_presenceCache.constEnd())
            return QVariant();
        return it.value();
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
    rebuildPresenceCache();
    if (manager)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { PresenceRole });
}

void MemberListModel::setRoleColorProvider(RoleColorProvider provider)
{
    m_roleColorProvider = std::move(provider);
    if (manager)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), { RoleBadgeColorRole });
}

void MemberListModel::cachePresence(Core::Snowflake userId)
{
    if (!m_presenceProvider)
        return;
    const auto p = m_presenceProvider(userId);
    if (!p) {
        m_presenceCache.remove(userId);
        return;
    }
    m_presenceCache.insert(userId, buildPresenceMap(*p));
}

void MemberListModel::rebuildPresenceCache()
{
    m_presenceCache.clear();
    if (!manager || !m_presenceProvider)
        return;
    const int total = manager->totalItemCount();
    for (int row = 0; row < total; ++row) {
        const auto *item = manager->itemAt(row);
        if (item && item->type == Core::MemberListItem::Type::Member)
            cachePresence(item->userId);
    }
}

void MemberListModel::notifyPresenceChanged(Core::Snowflake userId)
{
    if (!manager)
        return;
    // Refresh the cached payload once per change; data(PresenceRole) reads it
    // in O(1) instead of rebuilding the map on every paint.
    cachePresence(userId);
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
    rebuildPresenceCache();
    endResetModel();
}

void MemberListModel::onListRowsChanged(const QList<int> &rows)
{
    if (!manager)
        return;
    const int total = manager->totalItemCount();
    for (int row : rows) {
        if (row < 0 || row >= total)
            continue;
        const auto *item = manager->itemAt(row);
        if (item && item->type == Core::MemberListItem::Type::Member)
            cachePresence(item->userId);
        emit dataChanged(index(row, 0), index(row, 0));
    }
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
    connect(manager, &Core::MemberListManager::listRowsChanged,
            this, &MemberListModel::onListRowsChanged);
}

void MemberListModel::disconnectManager()
{
    if (!manager)
        return;

    disconnect(manager, nullptr, this, nullptr);
}

} // namespace UI
} // namespace Acheron

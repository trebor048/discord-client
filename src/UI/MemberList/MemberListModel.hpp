#pragma once

#include <QAbstractListModel>

#include <functional>
#include <optional>

#include "Core/MemberListManager.hpp"
#include "Core/ImageManager.hpp"
#include "Core/ClientInstance.hpp"
#include "UI/AvatarRequestTracker.hpp"

namespace Acheron {
namespace UI {

class MemberListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        ItemTypeRole = Qt::UserRole + 1,
        UserIdRole,
        UsernameRole,
        AvatarRole,
        RoleColorRole,
        RoleIconRole,
        PresenceRole,
        GroupNameRole,
        GroupCountRole,
        GroupColorRole,
        LoadedRole,
        RoleBadgeColorRole,
    };

    explicit MemberListModel(Core::ImageManager *imageManager, QObject *parent = nullptr);

    void setManager(Core::MemberListManager *manager);
    // Provider returning the presence for a user (may be std::nullopt).
    using PresenceProvider =
            std::function<std::optional<Core::ClientInstance::UserPresence>(Core::Snowflake)>;
    void setPresenceProvider(PresenceProvider provider);

    // Provider returning the highest role color for a member (may be invalid).
    using RoleColorProvider =
            std::function<QColor(Core::Snowflake userId, Core::Snowflake guildId)>;
    void setRoleColorProvider(RoleColorProvider provider);

    void notifyPresenceChanged(Core::Snowflake userId);

    [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

private:
    void onListAboutToReset();
    void onListReset();
    void onImageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

    void connectManager();
    void disconnectManager();

    Core::MemberListManager *manager = nullptr;
    Core::ImageManager *imageManager;

    PresenceProvider m_presenceProvider;
    RoleColorProvider m_roleColorProvider;
    mutable AvatarRequestTracker<QPersistentModelIndex> avatarTracker;
    mutable AvatarRequestTracker<QPersistentModelIndex> roleIconTracker;
};

} // namespace UI
} // namespace Acheron

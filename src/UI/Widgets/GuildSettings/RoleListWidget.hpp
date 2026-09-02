#pragma once

#include <QListWidget>
#include <QPushButton>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
}

namespace UI {
namespace Widgets {

class RoleListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RoleListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                            QWidget *parent = nullptr);

    void refreshRoles();

signals:
    void roleSelected(Core::Snowflake roleId);
    void roleCreated();
    void roleDeleted(Core::Snowflake roleId);

private slots:
    void onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onCreateRole();
    void onDeleteRole();
    void onRoleCreated(const Discord::GuildRoleCreate &event);
    void onRoleUpdated(const Discord::GuildRoleUpdate &event);
    void onRoleDeleted(const Discord::GuildRoleDelete &event);

private:
    void setupUi();
    void populateList();
    // Persists the new role order after a drag-and-drop reorder.
    void persistOrder();

    Core::ClientInstance *m_instance;
    Core::Snowflake m_guildId;

    QListWidget *m_roleList = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QList<Discord::Role> m_roles;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

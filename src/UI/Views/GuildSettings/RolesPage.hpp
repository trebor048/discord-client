#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

class QSplitter;
class QStackedWidget;

namespace Acheron {
namespace UI {
namespace Widgets {
class RoleListWidget;
class RoleEditorWidget;
}

namespace Views {

class RolesPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit RolesPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                       QWidget *parent = nullptr);

    void load() override;

private slots:
    void onRoleSelected(Core::Snowflake roleId);
    void onRoleCreated();
    void onRoleDeleted(Core::Snowflake roleId);

private:
    void setupUi();

    QSplitter *m_splitter = nullptr;
    Widgets::RoleListWidget *m_roleList = nullptr;
    Widgets::RoleEditorWidget *m_roleEditor = nullptr;
};

} // namespace Views
} // namespace UI
} // namespace Acheron

#include "RolesPage.hpp"

#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "UI/Widgets/GuildSettings/RoleEditorWidget.hpp"
#include "UI/Widgets/GuildSettings/RoleListWidget.hpp"

namespace Acheron {
namespace UI {
namespace Views {

RolesPage::RolesPage(Core::ClientInstance *instance, Core::Snowflake guildId, QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();
}

void RolesPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *title = new QLabel(QStringLiteral("Roles"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold; padding: 16px 24px;"));
    layout->addWidget(title);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    m_roleList = new Widgets::RoleListWidget(m_instance, m_guildId, m_splitter);
    m_roleEditor = new Widgets::RoleEditorWidget(m_instance, m_guildId, m_splitter);

    m_splitter->addWidget(m_roleList);
    m_splitter->addWidget(m_roleEditor);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({250, 500});

    layout->addWidget(m_splitter, 1);

    connect(m_roleList, &Widgets::RoleListWidget::roleSelected, this, &RolesPage::onRoleSelected);
    connect(m_roleList, &Widgets::RoleListWidget::roleCreated, this, &RolesPage::onRoleCreated);
    connect(m_roleList, &Widgets::RoleListWidget::roleDeleted, this, &RolesPage::onRoleDeleted);
    connect(m_roleEditor, &Widgets::RoleEditorWidget::roleModified, m_roleList,
            &Widgets::RoleListWidget::refreshRoles);
}

void RolesPage::load()
{
    m_roleList->refreshRoles();
}

void RolesPage::onRoleSelected(Core::Snowflake roleId)
{
    m_roleEditor->loadRole(roleId);
}

void RolesPage::onRoleCreated()
{
    // The gateway GUILD_ROLE_CREATE event will trigger refreshRoles automatically
    emit statusMessage(QStringLiteral("Creating role..."));
}

void RolesPage::onRoleDeleted(Core::Snowflake roleId)
{
    m_roleEditor->clearRole();
    m_instance->discord()->deleteRole(m_guildId, roleId);
    emit statusMessage(QStringLiteral("Deleting role..."));
}

} // namespace Views
} // namespace UI
} // namespace Acheron

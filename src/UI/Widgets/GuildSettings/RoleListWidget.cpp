#include "RoleListWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

RoleListWidget::RoleListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                               QWidget *parent)
    : QWidget(parent)
    , m_instance(instance)
    , m_guildId(guildId)
{
    setupUi();

    auto *client = m_instance->discord();
    connect(client, &Discord::Client::guildRoleCreated, this, &RoleListWidget::onRoleCreated);
    connect(client, &Discord::Client::guildRoleUpdated, this, &RoleListWidget::onRoleUpdated);
    connect(client, &Discord::Client::guildRoleDeleted, this, &RoleListWidget::onRoleDeleted);
}

void RoleListWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_roleList = new QListWidget(this);
    m_roleList->setObjectName(QStringLiteral("roleList"));
    m_roleList->setUniformItemSizes(true);
    m_roleList->setDragDropMode(QAbstractItemView::InternalMove);
    m_roleList->setDefaultDropAction(Qt::MoveAction);
    m_roleList->setAlternatingRowColors(true);
    layout->addWidget(m_roleList, 1);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(8, 8, 8, 8);
    m_createButton = new QPushButton(QStringLiteral("+ New Role"), this);
    m_deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    m_deleteButton->setEnabled(false);
    btnLayout->addWidget(m_createButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_deleteButton);
    layout->addLayout(btnLayout);

    connect(m_roleList, &QListWidget::currentItemChanged, this,
            &RoleListWidget::onCurrentItemChanged);
    connect(m_createButton, &QPushButton::clicked, this, &RoleListWidget::onCreateRole);
    connect(m_deleteButton, &QPushButton::clicked, this, &RoleListWidget::onDeleteRole);
    // InternalMove lets the user drag roles to reorder, but without this the
    // new order was purely cosmetic — it vanished on the next refreshRoles()
    // (which re-sorts by server position) and was never sent to Discord.
    connect(m_roleList->model(), &QAbstractItemModel::rowsMoved, this,
            [this](const QModelIndex &, int, int, const QModelIndex &, int) { persistOrder(); });
}

void RoleListWidget::persistOrder()
{
    // The list shows the highest-position role at the top (see populateList's
    // descending sort); send the full new order with normalized positions so
    // the server reflects the drag exactly.
    QList<QPair<Core::Snowflake, int>> positions;
    const int count = m_roleList->count();
    for (int i = 0; i < count; ++i) {
        if (QListWidgetItem *item = m_roleList->item(i)) {
            const Snowflake roleId(item->data(Qt::UserRole).toULongLong());
            if (roleId.isValid())
                positions.append({ roleId, count - 1 - i });
        }
    }
    if (!positions.isEmpty())
        m_instance->discord()->reorderRoles(m_guildId, positions);
}

void RoleListWidget::refreshRoles()
{
    m_roles = m_instance->getRolesForGuild(m_guildId);
    populateList();
}

void RoleListWidget::populateList()
{
    m_roleList->clear();

    // Sort by position descending (highest position = top)
    auto sortedRoles = m_roles;
    std::sort(sortedRoles.begin(), sortedRoles.end(),
              [](const Discord::Role &a, const Discord::Role &b) {
                  return a.position.get() > b.position.get();
              });

    for (const auto &role : sortedRoles) {
        auto *item = new QListWidgetItem(m_roleList);
        item->setText(role.name.getOr(QStringLiteral("new role")));

        // Color dot if role has a color
        if (role.hasColor()) {
            QColor c = role.getColor();
            QPixmap dot(12, 12);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setBrush(c);
            p.setPen(Qt::NoPen);
            p.drawEllipse(0, 0, 12, 12);
            item->setIcon(QIcon(dot));
        }

        item->setData(Qt::UserRole, static_cast<quint64>(role.id.get()));
        // Don't allow deleting @everyone role
        if (role.id.get() == m_guildId)
            item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
    }
}

void RoleListWidget::onCurrentItemChanged(QListWidgetItem *current, QListWidgetItem * /*previous*/)
{
    if (!current) {
        m_deleteButton->setEnabled(false);
        return;
    }

    const quint64 roleId = current->data(Qt::UserRole).toULongLong();
    m_deleteButton->setEnabled(roleId != static_cast<quint64>(m_guildId));
    emit roleSelected(Snowflake(roleId));
}

void RoleListWidget::onCreateRole()
{
    m_instance->discord()->createRole(m_guildId, QStringLiteral("new role"),
                                      Discord::Permissions(Discord::NO_PERMISSIONS), 0,
                                      false, false);
    emit roleCreated();
}

void RoleListWidget::onDeleteRole()
{
    auto *current = m_roleList->currentItem();
    if (!current)
        return;

    const quint64 roleId = current->data(Qt::UserRole).toULongLong();
    if (roleId == static_cast<quint64>(m_guildId))
        return;

    emit roleDeleted(Snowflake(roleId));
}

void RoleListWidget::onRoleCreated(const Discord::GuildRoleCreate &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    refreshRoles();
}

void RoleListWidget::onRoleUpdated(const Discord::GuildRoleUpdate &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    refreshRoles();
}

void RoleListWidget::onRoleDeleted(const Discord::GuildRoleDelete &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    refreshRoles();
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron

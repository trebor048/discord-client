#include "IntegrationsWidget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

IntegrationsWidget::IntegrationsWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                                       QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();

    auto *client = m_instance->discord();
    connect(client, &Discord::Client::integrationCreated, this,
            &IntegrationsWidget::onIntegrationCreated);
    connect(client, &Discord::Client::integrationDeleted, this,
            &IntegrationsWidget::onIntegrationDeleted);
    connect(client, &Discord::Client::integrationUpdated, this,
            &IntegrationsWidget::onIntegrationUpdated);
    connect(client, &Discord::Client::guildIntegrationsFetched, this,
            &IntegrationsWidget::onIntegrationsFetched);
}

void IntegrationsWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Integrations"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    m_integrationList = new QListWidget(this);
    m_integrationList->setAlternatingRowColors(true);
    m_integrationList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_integrationList, 1);

    auto *btnLayout = new QHBoxLayout();
    m_deleteButton = new QPushButton(QStringLiteral("Remove"), this);
    m_deleteButton->setEnabled(false);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
    btnLayout->addStretch();
    btnLayout->addWidget(m_deleteButton);
    btnLayout->addWidget(m_refreshButton);
    layout->addLayout(btnLayout);

    connect(m_integrationList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
                m_deleteButton->setEnabled(current != nullptr);
            });
    connect(m_deleteButton, &QPushButton::clicked, this, &IntegrationsWidget::onDeleteIntegration);
    connect(m_refreshButton, &QPushButton::clicked, this, &IntegrationsWidget::load);
}

void IntegrationsWidget::load()
{
    m_integrationList->clear();
    auto *item = new QListWidgetItem(m_integrationList);
    item->setText(QStringLiteral("Loading integrations..."));
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);

    m_instance->discord()->fetchGuildIntegrations(m_guildId, [](const auto &) {});
}

void IntegrationsWidget::onIntegrationsFetched(Core::Snowflake guildId,
                                               const QList<Discord::IntegrationData> &integrations)
{
    if (guildId != m_guildId)
        return;
    m_integrations = integrations;
    populateList(integrations);
}

void IntegrationsWidget::populateList(const QList<Discord::IntegrationData> &integrations)
{
    m_integrationList->clear();

    for (const auto &integration : integrations) {
        const QString name = integration.name.getOr(QStringLiteral("Unknown"));
        const QString type = integration.type.getOr(QStringLiteral("unknown"));
        const bool enabled = integration.enabled.getOr(false);

        QString status = enabled ? QStringLiteral("Enabled") : QStringLiteral("Disabled");
        if (integration.syncing.getOr(false))
            status += QStringLiteral(" (Syncing)");

        auto *item = new QListWidgetItem(m_integrationList);
        item->setText(QStringLiteral("%1 (%2) — %3").arg(name, type, status));
        item->setData(Qt::UserRole, static_cast<quint64>(integration.id.get()));
    }

    if (m_integrationList->count() == 0) {
        auto *item = new QListWidgetItem(m_integrationList);
        item->setText(QStringLiteral("No integrations configured."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

void IntegrationsWidget::onDeleteIntegration()
{
    auto *current = m_integrationList->currentItem();
    if (!current)
        return;

    const quint64 integrationId = current->data(Qt::UserRole).toULongLong();
    m_instance->discord()->deleteIntegration(m_guildId, Snowflake(integrationId));
    emit statusMessage(QStringLiteral("Removing integration..."));
}

void IntegrationsWidget::onIntegrationCreated(const Discord::IntegrationCreate &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    load();
}

void IntegrationsWidget::onIntegrationDeleted(const Discord::IntegrationDelete &event)
{
    if (event.guildId.get() != m_guildId)
        return;

    const quint64 integrationId = static_cast<quint64>(event.id.get());

    // Remove from list widget
    for (int i = 0; i < m_integrationList->count(); ++i) {
        auto *item = m_integrationList->item(i);
        if (item->data(Qt::UserRole).toULongLong() == integrationId) {
            delete m_integrationList->takeItem(i);
            break;
        }
    }

    // Also update m_integrations to keep it in sync
    m_integrations.removeIf(
        [integrationId](const Discord::IntegrationData &i) {
            return static_cast<quint64>(i.id.get()) == integrationId;
        });

    if (m_integrationList->count() == 0) {
        auto *item = new QListWidgetItem(m_integrationList);
        item->setText(QStringLiteral("No integrations configured."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

void IntegrationsWidget::onIntegrationUpdated(const Discord::IntegrationUpdate &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    load();
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron

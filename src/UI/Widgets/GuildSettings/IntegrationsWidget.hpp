#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Discord {
struct IntegrationCreate;
struct IntegrationDelete;
struct IntegrationUpdate;
}

namespace UI {
namespace Widgets {

class IntegrationsWidget : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit IntegrationsWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                                QWidget *parent = nullptr);

    void load() override;

private slots:
    void onDeleteIntegration();
    void onIntegrationCreated(const Discord::IntegrationCreate &event);
    void onIntegrationDeleted(const Discord::IntegrationDelete &event);
    void onIntegrationUpdated(const Discord::IntegrationUpdate &event);
    void onIntegrationsFetched(Core::Snowflake guildId,
                               const QList<Discord::IntegrationData> &integrations);

private:
    void setupUi();
    void populateList(const QList<Discord::IntegrationData> &integrations);

    QListWidget *m_integrationList = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QList<Discord::IntegrationData> m_integrations;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

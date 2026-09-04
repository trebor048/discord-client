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

private:
    void setupUi();
    void populateList(const QList<Discord::IntegrationData> &integrations);

    QListWidget *m_integrationList = nullptr;
    QPushButton *m_deleteButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QList<Discord::IntegrationData> m_integrations;

    /// Bumped on every load(); responses whose token is stale are dropped so a
    /// slow reply can never clobber the result of a newer refresh, and a reply
    /// that arrives after the widget was destroyed is a safe no-op.
    quint64 m_fetchGeneration = 0;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

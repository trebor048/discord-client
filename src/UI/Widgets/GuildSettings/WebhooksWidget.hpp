#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QDialog;
class QLineEdit;
class QComboBox;

namespace Acheron {
namespace Discord {
struct WebhooksUpdate;
}

namespace UI {
namespace Widgets {

class WebhooksWidget : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit WebhooksWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                            QWidget *parent = nullptr);

    void load() override;

private slots:
    void onCreateWebhook();
    void onDeleteWebhook();
    void onEditWebhook();
    void onWebhooksUpdated(const Discord::WebhooksUpdate &event);

private:
    void setupUi();
    void populateList(const QList<Discord::WebhookData> &webhooks);

    QListWidget *m_webhookList = nullptr;
    QPushButton *m_createButton = nullptr;
    QPushButton *m_editButton = nullptr;
    QPushButton *m_deleteButton = nullptr;

    QList<Discord::WebhookData> m_webhooks;

    /// Bumped on every load(); responses whose token is stale are dropped so a
    /// slow reply can never clobber the result of a newer refresh, and a reply
    /// that arrives after the widget was destroyed is a safe no-op.
    quint64 m_fetchGeneration = 0;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Discord {
struct InviteCreate;
struct InviteDelete;
}

namespace UI {
namespace Widgets {

class InvitesListWidget : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit InvitesListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                               QWidget *parent = nullptr);

    void load() override;

private slots:
    void onRevokeClicked();
    void onInviteCreated(const Discord::InviteCreate &event);
    void onInviteDeleted(const Discord::InviteDelete &event);
    void onInvitesFetched(Core::Snowflake guildId, const QList<Discord::InviteData> &invites);

private:
    void setupUi();
    void populateList(const QList<Discord::InviteData> &invites);

    QListWidget *m_inviteList = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_revokeButton = nullptr;
    QList<Discord::InviteData> m_invites;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

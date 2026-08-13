#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

#include "Discord/Entities.hpp"
#include "Discord/Events.hpp"

class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Discord {
struct GuildBan;
}

namespace UI {
namespace Widgets {

class BanListWidget : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit BanListWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                           QWidget *parent = nullptr);

    void load() override;

private slots:
    void onUnbanClicked();
    void onBanAdded(const Discord::GuildBan &event);
    void onBanRemoved(const Discord::GuildBan &event);
    void onBansFetched(Core::Snowflake guildId, const QList<Discord::BanEntry> &bans);

private:
    void setupUi();
    void populateList(const QList<Discord::BanEntry> &bans);

    QListWidget *m_banList = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_unbanButton = nullptr;
    QList<Discord::BanEntry> m_bans;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

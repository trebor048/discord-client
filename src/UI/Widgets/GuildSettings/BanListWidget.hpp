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

private:
    void setupUi();
    void populateList(const QList<Discord::BanEntry> &bans);

    QListWidget *m_banList = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_unbanButton = nullptr;
    QList<Discord::BanEntry> m_bans;

    /// Bumped on every load(); responses whose token is stale are dropped so a
    /// slow reply can never clobber the result of a newer refresh, and a reply
    /// that arrives after the widget was destroyed is a safe no-op.
    quint64 m_fetchGeneration = 0;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

#pragma once

#include <QComboBox>
#include <QListWidget>
#include <QPushButton>

#include "UI/Dialogs/GuildSettingsPage.hpp"

#include "Discord/Entities.hpp"

class QListWidgetItem;

namespace Acheron {
namespace Discord {
struct AuditLogData;
}

namespace UI {
namespace Widgets {

class AuditLogWidget : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit AuditLogWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                            QWidget *parent = nullptr);

    void load() override;

private slots:
    void onLoadMore();
    void onFilterChanged();

private:
    void setupUi();
    void fetchPage(Core::Snowflake beforeId);
    void appendEntries(const QList<Discord::AuditLogEntry> &entries,
                       const QHash<Core::Snowflake, Discord::User> &userMap);
    static QString actionTypeToString(int actionType);
    static QString formatEntry(const Discord::AuditLogEntry &entry,
                               const QHash<Core::Snowflake, Discord::User> &userMap);

    QComboBox *m_userFilterCombo = nullptr;
    QComboBox *m_actionFilterCombo = nullptr;
    QListWidget *m_logList = nullptr;
    QPushButton *m_loadMoreButton = nullptr;

    Core::Snowflake m_lastEntryId;
    QHash<Core::Snowflake, Discord::User> m_userMap;
    bool m_loading = false;

    /// Bumped on every page request. A response whose token is stale is dropped:
    /// filter changes, page revisits and load-more clicks can otherwise let a
    /// slow older response append under the wrong filter, and a response that
    /// arrives after the widget was destroyed would touch freed memory.
    quint64 m_fetchToken = 0;
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron

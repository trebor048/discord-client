#pragma once

#include "UI/Dialogs/GuildSettingsPage.hpp"

class QLabel;
class QLineEdit;
class QPushButton;

namespace Acheron {
namespace UI {
namespace Views {

class OverviewPage : public GuildSettingsPage
{
    Q_OBJECT
public:
    explicit OverviewPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                          QWidget *parent = nullptr);

    void load() override;

private slots:
    void onSaveName();

private:
    void setupUi();

    QLabel *m_iconLabel = nullptr;
    QLabel *m_guildIdLabel = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLabel *m_ownerLabel = nullptr;
    QLabel *m_memberCountLabel = nullptr;
    QLabel *m_premiumTierLabel = nullptr;
};

} // namespace Views
} // namespace UI
} // namespace Acheron

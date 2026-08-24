#pragma once

#include <QColor>
#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>

#include "Core/Snowflake.hpp"
#include "Core/ClientInstance.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Enums.hpp"

class QLabel;
class QNetworkAccessManager;
class QPushButton;
class QSplitter;

namespace Acheron {
namespace UI {

class GuildSettingsPage;

class GuildSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Page {
        Overview = 0,
        Roles,
        Emoji,
        Stickers,
        Bans,
        Invites,
        AuditLog,
        Webhooks,
        Integrations,
    };

    explicit GuildSettingsDialog(Core::ClientInstance *instance, Core::Snowflake guildId,
                                 QWidget *parent = nullptr);
    ~GuildSettingsDialog() override = default;

    [[nodiscard]] Core::Snowflake guildId() const { return m_guildId; }

    void setCurrentPage(Page page);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onPageChanged(int index);
    void onStatusMessage(const QString &message);

private:
    void setupUi();
    void addPage(Page page, const QString &title, GuildSettingsPage *widget);
    void applyPermissions();

    Core::ClientInstance *m_instance;
    Core::Snowflake m_guildId;

    QSplitter *m_splitter = nullptr;
    QListWidget *m_navList = nullptr;
    QStackedWidget *m_pageStack = nullptr;
    QLabel *m_guildNameLabel = nullptr;
    QLabel *m_guildIconLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_closeButton = nullptr;
    QNetworkAccessManager *m_iconLoader = nullptr;

    Discord::Permissions m_myPermissions;
};

} // namespace UI
} // namespace Acheron

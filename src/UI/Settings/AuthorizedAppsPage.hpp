#pragma once

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Discord {
class Client;
}
namespace UI {

class AuthorizedAppsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AuthorizedAppsPage(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);

private:
    void refreshApps();
    void addAppEntry(const QJsonObject &app);
    void onRevokeApp(quint64 appId);

    Discord::Client *client = nullptr;

    QListWidget *appsList;
    QPushButton *refreshButton;
    QLabel *statusLabel;
};

} // namespace UI
} // namespace Acheron

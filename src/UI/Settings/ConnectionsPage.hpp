#pragma once

#include <QJsonObject>
#include <QList>
#include <QPointer>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QNetworkAccessManager;
class QPushButton;

namespace Acheron {
namespace Discord {
class Client;
class ConnectedAccount;
}
namespace UI {

class ConnectionsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ConnectionsPage(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);

signals:
    void connectionsChanged();

private:
    void refreshConnections();
    void onRemoveConnection(const QString &type, const QString &id);
    void addConnectionEntry(const QJsonObject &conn);

    QPointer<Discord::Client> client;

    QListWidget *connectionsList;
    QPushButton *refreshButton;
    QLabel *statusLabel;
    QNetworkAccessManager *m_nam;
};

} // namespace UI
} // namespace Acheron

#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace Acheron {
namespace Core {
class ImageManager;
}
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
    void setImageManager(Core::ImageManager *imageManager);

private:
    void refreshApps();
    void addAppEntry(const QJsonObject &app);
    void onRevokeApp(quint64 appId);

    QPointer<Discord::Client> client;
    Core::ImageManager *imageManager = nullptr;
    bool pendingRefresh = false;

    QListWidget *appsList;
    QPushButton *refreshButton;
    QLabel *statusLabel;
};

} // namespace UI
} // namespace Acheron

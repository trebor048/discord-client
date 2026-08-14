#pragma once

#include "UI/Dialogs/BasePopup.hpp"

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QString>

namespace Acheron {
namespace Discord {
class Client;
}
namespace Core {
class ImageManager;
class NotificationManager;
namespace AV {
class VoiceManager;
}
}
namespace UI {

class SettingsWindow : public BasePopup
{
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);
    void setImageManager(Core::ImageManager *mgr);
    void setNotificationManager(Core::NotificationManager *mgr);
    void setVoiceManager(Core::AV::VoiceManager *mgr);

signals:
    void channelListModeChanged(bool classic);
    void compactModeChanged(bool compact);
    void compactInputChanged(bool compact);
    void showTimestampsChanged(bool enabled);
    void notificationSoundsChanged(bool enabled);
    void customStatusChanged(const QString &status);
    void streamerModeChanged(bool enabled);
    void editProfileRequested();
    void newTabBehaviorChanged();
    void pushToTalkToggled(bool enabled);
    void pushToTalkKeyChanged(const QString &key);

private:
    void setupUi();

    QListWidget *categoryList;
    QStackedWidget *pages;
    Discord::Client *client = nullptr;
};

} // namespace UI
} // namespace Acheron

#pragma once

#include "UI/Dialogs/BasePopup.hpp"

#include <QWidget>
#include <QListWidget>
#include <QStackedWidget>
#include <QString>
#include <QStringList>

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

    /// Jump to the settings category whose label matches `name`
    /// (case-insensitive). No-op if no category matches.
    void selectPage(const QString &name);

    /// The category labels in display order (matches the side list).
    QStringList pageNames() const;

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

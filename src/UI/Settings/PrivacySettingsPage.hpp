#pragma once

#include <QWidget>

class QComboBox;
class QCheckBox;

namespace Acheron {
namespace Discord {
class Client;
}
namespace UI {

class PrivacySettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit PrivacySettingsPage(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);

signals:
    void privacyChanged();

private:
    void syncFriendRequestControls(int policy);
    void fetchSettings();

    Discord::Client *client = nullptr;

    QComboBox *dmFilterCombo;
    QComboBox *friendAddCombo;
    QComboBox *explicitImageFilterCombo;
    QCheckBox *allowDmFromServerMembers;
    QCheckBox *friendRequestFromEveryone;
    QCheckBox *friendRequestFromFriendsOfFriends;
    QCheckBox *friendRequestFromServerMembers;
};

} // namespace UI
} // namespace Acheron

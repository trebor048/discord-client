#pragma once

#include <QDialog>
#include <QJsonObject>

#include "Core/Snowflake.hpp"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextEdit;

namespace Acheron {
namespace Discord {
class Client;
}
namespace UI {

class EditProfileDialog : public QDialog
{
    Q_OBJECT
public:
    explicit EditProfileDialog(Discord::Client *client, QWidget *parent = nullptr);

private:
    void buildUi();
    void loadProfile();
    void onSaveClicked();
    void onAvatarUpload();
    void onBannerUpload();

    Discord::Client *client = nullptr;

    // Avatar
    QLabel *avatarPreview;
    QPushButton *avatarUploadBtn;
    QByteArray avatarData;

    // Banner
    QLabel *bannerPreview;
    QPushButton *bannerUploadBtn;
    QByteArray bannerData;

    // Display name / global name
    QLineEdit *displayNameEdit;

    // Bio
    QTextEdit *bioEdit;

    // Pronouns
    QLineEdit *pronounsEdit;

    // Accent color
    QComboBox *accentColorCombo;

    // Save
    QPushButton *saveBtn;
    QPushButton *cancelBtn;
};

} // namespace UI
} // namespace Acheron

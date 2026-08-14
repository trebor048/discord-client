#include "EditProfileDialog.hpp"

#include "Core/AnimationUtils.hpp"

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegion>
#include <QTextEdit>
#include <QVBoxLayout>

#include "Core/Result.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

EditProfileDialog::EditProfileDialog(Discord::Client *c, QWidget *parent)
    : QDialog(parent)
    , client(c)
{
    setWindowTitle(tr("Edit Profile"));
    setMinimumWidth(450);

    buildUi();
    loadProfile();
}

void EditProfileDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    // === Avatar section ===
    auto *avatarGroup = new QGroupBox(tr("Avatar"), this);
    auto *avatarLayout = new QHBoxLayout(avatarGroup);

    avatarPreview = new QLabel(avatarGroup);
    avatarPreview->setFixedSize(100, 100);
    avatarPreview->setStyleSheet(QStringLiteral(
        "border: 2px solid #ccc; border-radius: 50%; background: #3333;"));
    avatarPreview->setAlignment(Qt::AlignCenter);
    avatarLayout->addWidget(avatarPreview);

    avatarUploadBtn = new QPushButton(tr("Upload Avatar"), avatarGroup);
    avatarLayout->addWidget(avatarUploadBtn);
    avatarLayout->addStretch();

    layout->addWidget(avatarGroup);

    connect(avatarUploadBtn, &QPushButton::clicked, this, &EditProfileDialog::onAvatarUpload);

    // === Banner section ===
    auto *bannerGroup = new QGroupBox(tr("Banner"), this);
    auto *bannerLayout = new QHBoxLayout(bannerGroup);

    bannerPreview = new QLabel(bannerGroup);
    bannerPreview->setFixedSize(200, 60);
    bannerPreview->setStyleSheet(QStringLiteral(
        "border: 2px solid #ccc; background: #3333;"));
    bannerPreview->setAlignment(Qt::AlignCenter);
    bannerLayout->addWidget(bannerPreview);

    bannerUploadBtn = new QPushButton(tr("Upload Banner"), bannerGroup);
    bannerLayout->addWidget(bannerUploadBtn);
    bannerLayout->addStretch();

    layout->addWidget(bannerGroup);

    connect(bannerUploadBtn, &QPushButton::clicked, this, &EditProfileDialog::onBannerUpload);

    // === Profile info ===
    auto *infoGroup = new QGroupBox(tr("Profile Information"), this);
    auto *infoLayout = new QFormLayout(infoGroup);

    displayNameEdit = new QLineEdit(infoGroup);
    displayNameEdit->setPlaceholderText(tr("Display Name"));
    displayNameEdit->setMaxLength(32);
    infoLayout->addRow(tr("Display Name"), displayNameEdit);

    bioEdit = new QTextEdit(infoGroup);
    bioEdit->setMaximumHeight(100);
    bioEdit->setPlaceholderText(tr("About Me"));
    infoLayout->addRow(tr("About Me"), bioEdit);

    pronounsEdit = new QLineEdit(infoGroup);
    pronounsEdit->setPlaceholderText(tr("e.g., they/them"));
    pronounsEdit->setMaxLength(40);
    infoLayout->addRow(tr("Pronouns"), pronounsEdit);

    accentColorCombo = new QComboBox(infoGroup);
    accentColorCombo->addItem(tr("Default"), -1);
    accentColorCombo->addItem(tr("Blurple"), 0x5865F2);
    accentColorCombo->addItem(tr("Green"), 0x57F287);
    accentColorCombo->addItem(tr("Yellow"), 0xFEE75C);
    accentColorCombo->addItem(tr("Fuchsia"), 0xEB459E);
    accentColorCombo->addItem(tr("Red"), 0xED4245);
    accentColorCombo->addItem(tr("White"), 0xFFFFFF);
    accentColorCombo->addItem(tr("Black"), 0x000000);
    infoLayout->addRow(tr("Accent Color"), accentColorCombo);

    layout->addWidget(infoGroup);

    // === Buttons ===
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    cancelBtn = new QPushButton(tr("Cancel"), this);
    btnLayout->addWidget(cancelBtn);

    saveBtn = new QPushButton(tr("Save Changes"), this);
    saveBtn->setDefault(true);
    btnLayout->addWidget(saveBtn);

    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &EditProfileDialog::onSaveClicked);

    Acheron::Core::AnimationUtils::fadeIn(this, 180);
}

void EditProfileDialog::loadProfile()
{
    if (!client)
        return;

    QPointer<EditProfileDialog> guard(this);
    client->fetchOwnProfile([guard](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;

        if (!result.success() || !result.value)
            return;

        QJsonObject obj = result.value.value();
        QJsonObject userProfile = obj.value("user_profile").toObject();
        QJsonObject user = obj.value("user").toObject();

        guard->displayNameEdit->setText(user.value("global_name").toString());
        guard->bioEdit->setPlainText(userProfile.value("bio").toString());
        guard->pronounsEdit->setText(userProfile.value("pronouns").toString());

        int accent = userProfile.value("accent_color").toInt(-1);
        for (int i = 0; i < guard->accentColorCombo->count(); ++i) {
            if (guard->accentColorCombo->itemData(i).toInt() == accent) {
                guard->accentColorCombo->setCurrentIndex(i);
                break;
            }
        }
    });
}

bool EditProfileDialog::loadImageFile(const QString &path, QByteArray &outData,
                                      QString &outMimeType)
{
    constexpr qint64 kMaxImageBytes = 10 * 1024 * 1024; // 10 MB

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Could not open the selected image."));
        return false;
    }
    if (file.size() > kMaxImageBytes) {
        QMessageBox::warning(this, tr("Image Too Large"),
                             tr("The selected image is larger than 10 MB. "
                                "Please choose a smaller file."));
        return false;
    }

    // Detect the real format from file contents so the data URI carries the
    // correct MIME type (animated GIF/WebP avatars are not PNG).
    const QByteArray format = QImageReader::imageFormat(path).toLower();
    if (format == "png" || format == "gif" || format == "webp") {
        outMimeType = QStringLiteral("image/") + QString::fromLatin1(format);
    } else if (format == "jpeg" || format == "jpg") {
        outMimeType = QStringLiteral("image/jpeg");
    } else {
        QMessageBox::warning(this, tr("Unsupported Image"),
                             tr("The selected file is not a supported image "
                                "(PNG, JPEG, GIF, or WebP)."));
        return false;
    }

    outData = file.readAll();
    return true;
}

void EditProfileDialog::onAvatarUpload()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select Avatar Image"), {},
        tr("Images (*.png *.jpg *.jpeg *.gif *.webp)"));
    if (path.isEmpty())
        return;

    if (!loadImageFile(path, avatarData, avatarMimeType))
        return;

    QPixmap pix;
    pix.loadFromData(avatarData);
    if (!pix.isNull()) {
        // Show as circular crop
        QPixmap circular(100, 100);
        circular.fill(Qt::transparent);
        QPainter painter(&circular);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.setClipRegion(QRegion(0, 0, 100, 100, QRegion::Ellipse));
        painter.drawPixmap(0, 0, 100, 100, pix.scaled(100, 100, Qt::KeepAspectRatioByExpanding,
                                                        Qt::SmoothTransformation));
        avatarPreview->setPixmap(circular);
    } else {
        avatarData.clear();
        avatarMimeType.clear();
    }
}

void EditProfileDialog::onBannerUpload()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Select Banner Image"), {},
        tr("Images (*.png *.jpg *.jpeg *.gif *.webp)"));
    if (path.isEmpty())
        return;

    if (!loadImageFile(path, bannerData, bannerMimeType))
        return;

    QPixmap pix;
    pix.loadFromData(bannerData);
    if (!pix.isNull()) {
        bannerPreview->setPixmap(pix.scaled(200, 60, Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation));
    } else {
        bannerData.clear();
        bannerMimeType.clear();
    }
}

void EditProfileDialog::onSaveClicked()
{
    QJsonObject payload;

    // Display name (global_name via username field is not set here -- uses PATCH /users/@me)
    if (!displayNameEdit->text().isEmpty())
        payload["global_name"] = displayNameEdit->text();

    // Bio and pronouns go into user_profile via PATCH /users/@me
    // Discord API: bio is top-level field on /users/@me
    if (!bioEdit->toPlainText().isEmpty())
        payload["bio"] = bioEdit->toPlainText();

    // Avatar (base64 data URI)
    if (!avatarData.isEmpty()) {
        QByteArray b64 = avatarData.toBase64();
        payload["avatar"] = QStringLiteral("data:%1;base64,").arg(avatarMimeType)
                            + QString::fromLatin1(b64);
    }

    // Banner (base64 data URI) -- requires premium, may fail
    if (!bannerData.isEmpty()) {
        QByteArray b64 = bannerData.toBase64();
        payload["banner"] = QStringLiteral("data:%1;base64,").arg(bannerMimeType)
                            + QString::fromLatin1(b64);
    }

    // Accent color
    bool ok = false;
    int accent = accentColorCombo->currentData().toInt(&ok);
    if (ok && accent != -1)
        payload["accent_color"] = accent;

    if (!client) {
        QMessageBox::warning(this, tr("Error"), tr("Not connected to Discord."));
        return;
    }

    saveBtn->setEnabled(false);
    saveBtn->setText(tr("Saving..."));

    QPointer<EditProfileDialog> guard(this);
    client->updateProfile(payload, [guard](const Core::Result<QJsonObject> &result) {
        if (!guard)
            return;

        guard->saveBtn->setEnabled(true);
        guard->saveBtn->setText(tr("Save Changes"));

        if (result.success()) {
            guard->accept();
        } else {
            QMessageBox::warning(guard, tr("Error"),
                                 tr("Failed to update profile. Please try again."));
        }
    });
}

} // namespace UI
} // namespace Acheron

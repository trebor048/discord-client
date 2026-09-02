#include "SoundOverrideWidget.hpp"

#include "Core/Notification/SoundManager.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QFileInfo>
#include <QJsonObject>
#include <QLineEdit>
#include <QDebug>

SoundOverrideWidget::SoundOverrideWidget(const QString &soundId, const QString &displayName, QWidget *parent)
    : QWidget(parent), m_soundId(soundId), m_displayName(displayName)
{
    setupUi();
}

void SoundOverrideWidget::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    // Header with enable checkbox and name
    auto *headerLayout = new QHBoxLayout();
    
    m_enableCheck = new QCheckBox(this);
    m_enableCheck->setText(m_displayName.isEmpty() ? m_soundId : m_displayName);
    // Inherit the theme's UI font (family + size) instead of hardcoding a
    // generic family at a fixed size, which clashes with large UI fonts.
    {
        QFont headerFont = m_enableCheck->font();
        headerFont.setBold(true);
        m_enableCheck->setFont(headerFont);
    }
    headerLayout->addWidget(m_enableCheck);

    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);

    // Sound selection
    auto *soundLayout = new QHBoxLayout();
    soundLayout->addWidget(new QLabel(tr("Sound:"), this));
    
    m_soundCombo = new QComboBox(this);
    m_soundCombo->addItem(tr("Default"), "default");
    m_soundCombo->addItem(tr("Message 1 (Generic)"), "message1");
    m_soundCombo->addItem(tr("Message 2 (Reply)"), "message2");
    m_soundCombo->addItem(tr("Message 3 (DM)"), "message3");
    m_soundCombo->addItem(tr("Mention 1 (@role)"), "mention1");
    m_soundCombo->addItem(tr("Mention 2 (@everyone)"), "mention2");
    m_soundCombo->addItem(tr("Mention 3 (@here)"), "mention3");
    m_soundCombo->addItem(tr("Custom File..."), "custom");
    soundLayout->addWidget(m_soundCombo, 1);
    mainLayout->addLayout(soundLayout);

    // Volume
    auto *volumeLayout = new QHBoxLayout();
    volumeLayout->addWidget(new QLabel(tr("Volume:"), this));
    
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(100);
    m_volumeSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    volumeLayout->addWidget(m_volumeSlider, 1);
    
    m_volumeLabel = new QLabel("100%", this);
    m_volumeLabel->setFixedWidth(40);
    volumeLayout->addWidget(m_volumeLabel);
    volumeLayout->addStretch();
    mainLayout->addLayout(volumeLayout);

    // Custom file
    auto *customLayout = new QHBoxLayout();
    customLayout->addWidget(new QLabel(tr("Custom File:"), this));
    
    m_customFileBtn = new QPushButton(tr("Choose File..."), this);
    m_customFileBtn->setEnabled(false);
    customLayout->addWidget(m_customFileBtn);
    
    m_customFileLabel = new QLabel(tr("No file selected"), this);
    m_customFileLabel->setStyleSheet("color: #72767d; font-style: italic;");
    customLayout->addWidget(m_customFileLabel, 1);
    mainLayout->addLayout(customLayout);

    auto *urlLayout = new QHBoxLayout();
    urlLayout->addWidget(new QLabel(tr("Or URL:"), this));
    m_customUrlEdit = new QLineEdit(this);
    m_customUrlEdit->setPlaceholderText("https://example.com/sound.mp3");
    m_customUrlEdit->setEnabled(false);
    urlLayout->addWidget(m_customUrlEdit, 1);
    m_useUrlBtn = new QPushButton(tr("Use URL"), this);
    m_useUrlBtn->setEnabled(false);
    urlLayout->addWidget(m_useUrlBtn);
    mainLayout->addLayout(urlLayout);

    // Test button
    auto *testLayout = new QHBoxLayout();
    testLayout->addStretch();
    m_testBtn = new QPushButton(tr("Test Play"), this);
    testLayout->addWidget(m_testBtn);
    mainLayout->addLayout(testLayout);

    // Connect signals
    connect(m_enableCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_soundCombo->setEnabled(checked);
        m_volumeSlider->setEnabled(checked);
        m_volumeLabel->setEnabled(checked);
        emit changed();
    });
    
    connect(m_soundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        bool isCustom = (m_soundCombo->currentData().toString() == "custom");
        m_customFileBtn->setEnabled(isCustom);
        m_customFileLabel->setEnabled(isCustom);
        m_customUrlEdit->setEnabled(isCustom);
        m_useUrlBtn->setEnabled(isCustom);
        emit changed();
    });
    
    connect(m_volumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_volumeLabel->setText(QString("%1%").arg(value));
        emit changed();
    });
    
    connect(m_useUrlBtn, &QPushButton::clicked, this, [this]() {
        const QString url = m_customUrlEdit->text().trimmed();
        if (url.isEmpty())
            return;
        m_customUrl = url;
        m_customFilePath.clear();
        updateCustomFileLabel();
        emit changed();
    });

    connect(m_customFileBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, tr("Select Custom Sound File"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            tr("Audio Files (*.mp3 *.ogg *.wav *.flac *.m4a);;All Files (*.*)"));
        if (!file.isEmpty()) {
            m_customFilePath = file;
            m_customUrl.clear();
            updateCustomFileLabel();
            emit changed();
        }
    });
    
    connect(m_testBtn, &QPushButton::clicked, this, &SoundOverrideWidget::onTestPlay);

    // Initial state
    m_soundCombo->setEnabled(false);
    m_volumeSlider->setEnabled(false);
    m_volumeLabel->setEnabled(false);
    m_customFileBtn->setEnabled(false);
}

void SoundOverrideWidget::updateCustomFileLabel()
{
    if (!m_customUrl.isEmpty()) {
        m_customFileLabel->setText(m_customUrl);
    } else if (m_customFilePath.isEmpty()) {
        m_customFileLabel->setText(tr("No file selected"));
    } else {
        m_customFileLabel->setText(QFileInfo(m_customFilePath).fileName());
    }
}

void SoundOverrideWidget::onTestPlay()
{
    QString sound = m_soundCombo->currentData().toString();
    if (sound == "custom" && !m_customUrl.isEmpty()) {
        playTestSound(QUrl(m_customUrl));
        return;
    }
    if (sound == "custom" && !m_customFilePath.isEmpty()) {
        QFile f(m_customFilePath);
        if (!f.exists()) {
            QMessageBox::warning(this, tr("Test Play"), tr("Custom file not found: %1").arg(m_customFilePath));
            return;
        }
        playTestSound(QUrl::fromLocalFile(m_customFilePath));
    } else if (m_soundManager) {
        // Preview a built-in sound through the real playback path so the
        // volume slider matches what notifications will actually sound like.
        QString playId = sound;
        if (playId == QLatin1String("default"))
            playId = QString::fromLatin1(Acheron::Core::SoundManager::DefaultNotification);
        m_soundManager->playNotificationSound(playId, m_volumeSlider->value());
    } else {
        QMessageBox::information(this, tr("Test Play"),
            tr("Connect a SoundManager to enable in-app preview. Sound: %1").arg(sound));
    }
}

void SoundOverrideWidget::playTestSound(const QUrl &source)
{
    cleanupTestPlayer();

    m_testPlayer = new QMediaPlayer(this);
    m_testOutput = new QAudioOutput(this);
    m_testPlayer->setAudioOutput(m_testOutput);
    m_testOutput->setVolume(m_volumeSlider->value() / 100.0);
    m_testPlayer->setSource(source);
    m_testPlayer->play();

    connect(m_testPlayer, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia)
                    cleanupTestPlayer();
            });
    connect(m_testPlayer, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                Q_UNUSED(error);
                qWarning() << "SoundOverrideWidget: test playback failed:" << errorString;
                cleanupTestPlayer();
            });
}

void SoundOverrideWidget::cleanupTestPlayer()
{
    if (!m_testPlayer)
        return;
    m_testPlayer->stop();
    m_testPlayer->deleteLater();
    m_testPlayer = nullptr;
    if (m_testOutput) {
        m_testOutput->deleteLater();
        m_testOutput = nullptr;
    }
}

void SoundOverrideWidget::loadFromJson(const QJsonObject &obj)
{
    m_enableCheck->setChecked(obj["enabled"].toBool(false));
    QString sound = obj["selected_sound"].toString("default");
    int index = m_soundCombo->findData(sound);
    if (index >= 0) m_soundCombo->setCurrentIndex(index);

    m_volumeSlider->setValue(obj["volume"].toInt(100));
    m_customFilePath = obj["custom_file_path"].toString();
    if (m_customFilePath.isEmpty())
        m_customFilePath = obj["custom_file_id"].toString();
    m_customUrl = obj["custom_url"].toString();
    if (!m_customUrl.isEmpty())
        m_customUrlEdit->setText(m_customUrl);
    updateCustomFileLabel();
    
    // Update UI state based on loaded values
    m_soundCombo->setEnabled(m_enableCheck->isChecked());
    m_volumeSlider->setEnabled(m_enableCheck->isChecked());
    m_volumeLabel->setEnabled(m_enableCheck->isChecked());
    bool isCustom = (m_soundCombo->currentData().toString() == "custom");
    m_customFileBtn->setEnabled(isCustom && m_enableCheck->isChecked());
    m_customFileLabel->setEnabled(isCustom && m_enableCheck->isChecked());
    m_customUrlEdit->setEnabled(isCustom && m_enableCheck->isChecked());
    m_useUrlBtn->setEnabled(isCustom && m_enableCheck->isChecked());
}

QJsonObject SoundOverrideWidget::toJson() const
{
    QJsonObject obj;
    obj["enabled"] = m_enableCheck->isChecked();
    obj["selected_sound"] = m_soundCombo->currentData().toString();
    obj["volume"] = m_volumeSlider->value();
    obj["custom_file_path"] = m_customFilePath;
    obj["custom_file_id"] = m_customFilePath;
    obj["custom_url"] = m_customUrl;
    return obj;
}

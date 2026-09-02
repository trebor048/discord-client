#pragma once

#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>

class QMediaPlayer;
class QAudioOutput;

namespace Acheron {
namespace Core {
class SoundManager;
}
}

class SoundOverrideWidget : public QWidget
{
    Q_OBJECT
public:
    explicit SoundOverrideWidget(const QString &soundId, const QString &displayName, QWidget *parent = nullptr);
    ~SoundOverrideWidget() override = default;

    QString soundId() const { return m_soundId; }
    bool isEnabled() const { return m_enableCheck->isChecked(); }
    QString selectedSound() const { return m_soundCombo->currentData().toString(); }
    int volume() const { return m_volumeSlider->value(); }
    QString customFilePath() const { return m_customFilePath; }
    void setCustomFilePath(const QString &path) { m_customFilePath = path; updateCustomFileLabel(); }
    QString customUrl() const { return m_customUrl; }
    void setCustomUrl(const QString &url) { m_customUrl = url; updateCustomFileLabel(); }

    // Enables in-app preview of the built-in sounds ("Test Play"); without a
    // manager the button only previews custom file/URL sources.
    void setSoundManager(Acheron::Core::SoundManager *manager) { m_soundManager = manager; }

    // Load/save from JSON
    void loadFromJson(const QJsonObject &obj);
    QJsonObject toJson() const;

signals:
    void changed();

private:
    void setupUi();
    void updateCustomFileLabel();
    void onTestPlay();
    void playTestSound(const QUrl &source);
    void cleanupTestPlayer();

    QMediaPlayer *m_testPlayer = nullptr;
    QAudioOutput *m_testOutput = nullptr;
    Acheron::Core::SoundManager *m_soundManager = nullptr;

    QString m_soundId;
    QString m_displayName;
    QString m_customFilePath;
    QString m_customUrl;

    QCheckBox *m_enableCheck = nullptr;
    QComboBox *m_soundCombo = nullptr;
    QSlider *m_volumeSlider = nullptr;
    QLabel *m_volumeLabel = nullptr;
    QPushButton *m_customFileBtn = nullptr;
    QLabel *m_customFileLabel = nullptr;
    QLineEdit *m_customUrlEdit = nullptr;
    QPushButton *m_useUrlBtn = nullptr;
    QPushButton *m_testBtn = nullptr;
};
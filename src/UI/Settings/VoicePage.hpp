#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;

namespace Acheron {
namespace Core {
namespace AV {
class VoiceManager;
}
} // namespace Core
namespace UI {

class VoicePage : public QWidget
{
    Q_OBJECT
public:
    explicit VoicePage(QWidget *parent = nullptr);
    void setVoiceManager(Core::AV::VoiceManager *manager);

signals:
    void inputDeviceChanged(const QString &id);
    void outputDeviceChanged(const QString &id);
    void pushToTalkToggled(bool enabled);
    void pushToTalkKeyChanged(const QString &key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void refreshDevices();

    Core::AV::VoiceManager *voiceManager = nullptr;
    QComboBox *inputDeviceCombo;
    QComboBox *outputDeviceCombo;
    QDoubleSpinBox *inputSensitivitySpin;
    QCheckBox *pushToTalkCheckbox;
    QLineEdit *pushToTalkKeyEdit;
    QPushButton *recordKeyButton;
    QCheckBox *echoCancellationCheckbox;
    QCheckBox *noiseSuppressionCheckbox;
};

} // namespace UI
} // namespace Acheron

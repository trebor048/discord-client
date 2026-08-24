#include "VoicePage.hpp"

#include "Core/AV/VoiceManager.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

bool VoicePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == recordKeyButton && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const QString keyText = QKeySequence(keyEvent->key()).toString();
        if (!keyText.isEmpty()) {
            recordKeyButton->setText(keyText);
            QSettings().setValue("voice/ptt_key", keyText);
            emit pushToTalkKeyChanged(keyText);
            recordKeyButton->releaseKeyboard();
            recordKeyButton->removeEventFilter(this);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

VoicePage::VoicePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // --- Input / Output ---
    auto *deviceGroup = new QGroupBox(tr("Audio Devices"), this);
    auto *deviceLayout = new QFormLayout(deviceGroup);
    deviceLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    deviceLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    inputDeviceCombo = new QComboBox(this);
    inputDeviceCombo->setEditable(true);
    deviceLayout->addRow(tr("Input device:"), inputDeviceCombo);

    outputDeviceCombo = new QComboBox(this);
    outputDeviceCombo->setEditable(true);
    deviceLayout->addRow(tr("Output device:"), outputDeviceCombo);

    layout->addWidget(deviceGroup);

    // --- Input sensitivity ---
    auto *sensitivityGroup = new QGroupBox(tr("Input Sensitivity"), this);
    auto *sensitivityLayout = new QFormLayout(sensitivityGroup);
    sensitivityLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    sensitivityLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    inputSensitivitySpin = new QDoubleSpinBox(this);
    inputSensitivitySpin->setRange(0.0, 100.0);
    inputSensitivitySpin->setSuffix(QStringLiteral("%"));
    inputSensitivitySpin->setValue(QSettings().value("voice/input_sensitivity", 30.0).toDouble());
    sensitivityLayout->addRow(tr("Sensitivity threshold:"), inputSensitivitySpin);

    layout->addWidget(sensitivityGroup);

    // --- Push-to-talk ---
    auto *pttGroup = new QGroupBox(tr("Push-to-Talk"), this);
    auto *pttLayout = new QFormLayout(pttGroup);
    pttLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    pttLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

    pushToTalkCheckbox = new QCheckBox(tr("Enable push-to-talk"), this);
    pushToTalkCheckbox->setChecked(QSettings().value("voice/push_to_talk", false).toBool());
    pttLayout->addRow(pushToTalkCheckbox);

    QString savedKey = QSettings().value("voice/ptt_key", "V").toString();
    recordKeyButton = new QPushButton(savedKey, this);
    pttLayout->addRow(tr("Key bind:"), recordKeyButton);

    layout->addWidget(pttGroup);

    // --- Processing ---
    auto *processingGroup = new QGroupBox(tr("Audio Processing"), this);
    auto *processingLayout = new QVBoxLayout(processingGroup);
    processingLayout->setSpacing(10);

    // No AEC implementation exists in the audio stack yet (miniaudio + rnnoise
    // provide neither echo cancellation nor a reference-signal path), so this
    // toggle is shown disabled instead of pretending to do something.
    echoCancellationCheckbox = new QCheckBox(tr("Echo cancellation (not yet implemented)"), this);
    echoCancellationCheckbox->setEnabled(false);
    echoCancellationCheckbox->setChecked(false);
    echoCancellationCheckbox->setToolTip(
            tr("The audio engine does not support echo cancellation yet."));
    processingLayout->addWidget(echoCancellationCheckbox);

    noiseSuppressionCheckbox = new QCheckBox(tr("Noise suppression"), this);
    noiseSuppressionCheckbox->setChecked(QSettings().value("voice/noise_suppression", true).toBool());
    processingLayout->addWidget(noiseSuppressionCheckbox);

    layout->addWidget(processingGroup);

    layout->addStretch();

    // --- Persist ---
    connect(inputDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                QString deviceId;
                if (index >= 0)
                    deviceId = inputDeviceCombo->itemData(index).toString();
                if (deviceId.isEmpty())
                    deviceId = inputDeviceCombo->currentText();
                QSettings().setValue("voice/input_device", deviceId);
                emit inputDeviceChanged(deviceId);
            });
    connect(outputDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                QString deviceId;
                if (index >= 0)
                    deviceId = outputDeviceCombo->itemData(index).toString();
                if (deviceId.isEmpty())
                    deviceId = outputDeviceCombo->currentText();
                QSettings().setValue("voice/output_device", deviceId);
                emit outputDeviceChanged(deviceId);
            });
    connect(this, &VoicePage::inputDeviceChanged, this, [this](const QString &deviceId) {
        if (voiceManager)
            voiceManager->setInputDevice(deviceId.toUtf8());
    });
    connect(this, &VoicePage::outputDeviceChanged, this, [this](const QString &deviceId) {
        if (voiceManager)
            voiceManager->setOutputDevice(deviceId.toUtf8());
    });
    connect(inputSensitivitySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double val) {
                QSettings().setValue("voice/input_sensitivity", val);
                if (voiceManager)
                    voiceManager->setVadSensitivity(static_cast<float>(val));
            });
    connect(pushToTalkCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("voice/push_to_talk", checked);
        emit pushToTalkToggled(checked);
    });
    connect(recordKeyButton, &QPushButton::clicked, this, [this]() {
        recordKeyButton->setText(tr("Press a key..."));
        recordKeyButton->grabKeyboard();
        recordKeyButton->installEventFilter(this);
    });
    connect(noiseSuppressionCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("voice/noise_suppression", checked);
        if (voiceManager)
            voiceManager->setNoiseSuppressionEnabled(checked);
    });

    refreshDevices();
}

void VoicePage::setVoiceManager(Core::AV::VoiceManager *manager)
{
    if (voiceManager == manager)
        return;

    if (voiceManager) {
        disconnect(this, nullptr, voiceManager, nullptr);
        disconnect(voiceManager, nullptr, this, nullptr);
    }

    voiceManager = manager;
    refreshDevices();

    if (!voiceManager)
        return;

    inputSensitivitySpin->setValue(QSettings().value("voice/input_sensitivity", 30.0).toDouble());
    pushToTalkCheckbox->setChecked(QSettings().value("voice/push_to_talk", false).toBool());
    noiseSuppressionCheckbox->setChecked(QSettings().value("voice/noise_suppression", true).toBool());

    // setValue() only emits when the value changed; push the current setting
    // explicitly so the manager always has it.
    voiceManager->setVadSensitivity(static_cast<float>(inputSensitivitySpin->value()));

    // Apply saved device + noise-suppression settings at startup. These are only
    // pushed on user interaction otherwise, so they silently reverted to the
    // backend defaults until the user touched the combos/checkboxes.
    const QString savedInput = QSettings().value("voice/input_device").toString();
    const QString savedOutput = QSettings().value("voice/output_device").toString();
    if (!savedInput.isEmpty())
        voiceManager->setInputDevice(savedInput.toUtf8());
    if (!savedOutput.isEmpty())
        voiceManager->setOutputDevice(savedOutput.toUtf8());
    voiceManager->setNoiseSuppressionEnabled(noiseSuppressionCheckbox->isChecked());

    connect(voiceManager, &Core::AV::VoiceManager::devicesChanged, this, &VoicePage::refreshDevices,
            Qt::UniqueConnection);
}

void VoicePage::refreshDevices()
{
    inputDeviceCombo->blockSignals(true);
    outputDeviceCombo->blockSignals(true);

    inputDeviceCombo->clear();
    outputDeviceCombo->clear();

    QString savedInput = QSettings().value("voice/input_device").toString();
    QString savedOutput = QSettings().value("voice/output_device").toString();

    if (voiceManager) {
        for (const auto &device : voiceManager->availableInputDevices()) {
            inputDeviceCombo->addItem(device.description, QString::fromLatin1(device.id));
        }
        for (const auto &device : voiceManager->availableOutputDevices()) {
            outputDeviceCombo->addItem(device.description, QString::fromLatin1(device.id));
        }
    }

    inputDeviceCombo->setEditText(savedInput);
    outputDeviceCombo->setEditText(savedOutput);

    if (voiceManager) {
        const auto currentInput = QString::fromLatin1(voiceManager->currentInputDevice());
        const auto currentOutput = QString::fromLatin1(voiceManager->currentOutputDevice());

        int inputIdx = inputDeviceCombo->findData(currentInput);
        if (inputIdx >= 0)
            inputDeviceCombo->setCurrentIndex(inputIdx);
        else
            inputDeviceCombo->setEditText(savedInput);

        int outputIdx = outputDeviceCombo->findData(currentOutput);
        if (outputIdx >= 0)
            outputDeviceCombo->setCurrentIndex(outputIdx);
        else
            outputDeviceCombo->setEditText(savedOutput);

    }

    inputDeviceCombo->blockSignals(false);
    outputDeviceCombo->blockSignals(false);
}

} // namespace UI
} // namespace Acheron

#include "StreamerModePage.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QDebug>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

StreamerModePage::StreamerModePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // === Streamer Mode toggle ===
    auto *mainGroup = new QGroupBox(tr("Streamer Mode"), this);
    auto *mainLayout = new QVBoxLayout(mainGroup);
    mainLayout->setSpacing(10);

    streamerModeCheckbox = new QCheckBox(tr("Enable Streamer Mode"), this);
    streamerModeCheckbox->setChecked(QSettings().value("streamer/enabled", false).toBool());
    mainLayout->addWidget(streamerModeCheckbox);

    detectionLabel = new QLabel(this);
    mainLayout->addWidget(detectionLabel);

    autoDetectCheckbox = new QCheckBox(tr("Automatically enable when OBS or XSplit is running"), this);
    autoDetectCheckbox->setChecked(QSettings().value("streamer/auto_detect", false).toBool());
    mainLayout->addWidget(autoDetectCheckbox);

    checkNowBtn = new QPushButton(tr("Check Now"), this);
    mainLayout->addWidget(checkNowBtn);

    layout->addWidget(mainGroup);

    // === Privacy options ===
    auto *privacyGroup = new QGroupBox(tr("Privacy"), this);
    auto *privacyLayout = new QVBoxLayout(privacyGroup);
    privacyLayout->setSpacing(10);

    hidePersonalInfoCheckbox = new QCheckBox(tr("Hide personal information (email, phone)"), this);
    hidePersonalInfoCheckbox->setChecked(QSettings().value("streamer/hide_personal_info", true).toBool());
    privacyLayout->addWidget(hidePersonalInfoCheckbox);

    hideInviteLinksCheckbox = new QCheckBox(tr("Hide invite links when sharing screen"), this);
    hideInviteLinksCheckbox->setChecked(QSettings().value("streamer/hide_invites", true).toBool());
    privacyLayout->addWidget(hideInviteLinksCheckbox);

    muteNotificationSoundsCheckbox = new QCheckBox(tr("Mute notification sounds"), this);
    muteNotificationSoundsCheckbox->setChecked(QSettings().value("streamer/mute_sounds", true).toBool());
    privacyLayout->addWidget(muteNotificationSoundsCheckbox);

    layout->addWidget(privacyGroup);

    layout->addStretch();

    // Wire up persistence
    connect(streamerModeCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings().setValue("streamer/enabled", checked);
        emit streamerModeChanged(checked);
    });
    connect(autoDetectCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("streamer/auto_detect", checked);
    });
    connect(hidePersonalInfoCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("streamer/hide_personal_info", checked);
    });
    connect(hideInviteLinksCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("streamer/hide_invites", checked);
    });
    connect(muteNotificationSoundsCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("streamer/mute_sounds", checked);
    });
    connect(checkNowBtn, &QPushButton::clicked, this, &StreamerModePage::checkForStreamingSoftware);
}

void StreamerModePage::checkForStreamingSoftware()
{
    // Detect OBS / Streamlabs / XSplit asynchronously so the UI thread never
    // blocks on process enumeration. (The previous Windows path used
    // EnumProcesses + GetModuleBaseNameW synchronously and also missed
    // obs.exe and Streamlabs OBS.)
    QStringList args;
#ifdef Q_OS_WIN
    args = { QStringLiteral("tasklist"), QStringLiteral("/FO"), QStringLiteral("CSV"),
             QStringLiteral("/NH") };
#else
    // pgrep is unavailable on stock macOS, so list processes via ps and match locally.
    args = { QStringLiteral("ps"), QStringLiteral("-axco"), QStringLiteral("comm") };
#endif
    auto *process = new QProcess(this);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        qDebug() << "StreamerModePage: process check failed to start";
        detectionLabel->setText(tr("No streaming software detected"));
        detectionLabel->setStyleSheet("color: gray;");
        process->deleteLater();
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process](int exitCode, QProcess::ExitStatus status) {
                bool found = false;
                if (status == QProcess::NormalExit && exitCode == 0) {
                    const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).toLower();
                    // obs64.exe / obs32.exe (OBS Studio), obs.exe (older 32-bit
                    // OBS), slobs.exe (Streamlabs OBS), xsplit (XSplit).
                    static const QRegularExpression pattern(
                            QStringLiteral("(obs64|obs32|\\bobs\\.exe|slobs|xsplit)"));
                    found = pattern.match(output).hasMatch();
                }
                detectionLabel->setText(found ? tr("OBS/XSplit detected!")
                                              : tr("No streaming software detected"));
                detectionLabel->setStyleSheet(found ? "color: green;" : "color: gray;");
                process->deleteLater();
            });
    process->start(args.takeFirst(), args);
}

} // namespace UI
} // namespace Acheron

#include "StreamerModePage.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace Acheron {
namespace UI {

StreamerModePage::StreamerModePage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    // === Streamer Mode toggle ===
    auto *mainGroup = new QGroupBox(tr("Streamer Mode"), this);
    auto *mainLayout = new QVBoxLayout(mainGroup);

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
#ifdef Q_OS_WIN
    // Enumerate processes and look for OBS or XSplit
    DWORD processes[1024];
    DWORD needed = 0;
    if (!EnumProcesses(processes, sizeof(processes), &needed))
        return;

    DWORD count = needed / sizeof(DWORD);
    bool found = false;

    for (DWORD i = 0; i < count; ++i) {
        if (processes[i] == 0)
            continue;

        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE,
                                       processes[i]);
        if (!hProcess)
            continue;

        wchar_t name[MAX_PATH] = {};
        if (GetModuleBaseNameW(hProcess, nullptr, name, MAX_PATH) > 0) {
            QString processName = QString::fromWCharArray(name).toLower();
            if (processName.contains("obs64") || processName.contains("obs32") ||
                processName.contains("xsplit")) {
                found = true;
            }
        }
        CloseHandle(hProcess);

        if (found)
            break;
    }

    if (found) {
        detectionLabel->setText(tr("OBS/XSplit detected!"));
        detectionLabel->setStyleSheet("color: green;");
    } else {
        detectionLabel->setText(tr("No streaming software detected"));
        detectionLabel->setStyleSheet("color: gray;");
    }
#else
    // On non-Windows platforms, use QProcess to check for OBS
    QProcess process;
    process.start("pgrep", QStringList{"-x", "obs"});
    process.waitForFinished(3000);
    if (process.exitCode() == 0) {
        detectionLabel->setText(tr("OBS detected!"));
        detectionLabel->setStyleSheet("color: green;");
    } else {
        detectionLabel->setText(tr("No streaming software detected"));
        detectionLabel->setStyleSheet("color: gray;");
    }
#endif
}

} // namespace UI
} // namespace Acheron

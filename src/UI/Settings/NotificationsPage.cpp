#include "NotificationsPage.hpp"
#include "SoundOverrideWidget.hpp"

#include "Core/Notification/NotificationManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QPushButton>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QInputDialog>
#include <QMenu>
#include <QScrollArea>
#include <QStandardPaths>
#include <QApplication>
#include <QSystemTrayIcon>

namespace Acheron {
namespace UI {

NotificationsPage::NotificationsPage(QWidget *parent)
    : QWidget(parent)
{
    setupUi();
    loadSettings();
}

void NotificationsPage::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // Create tabs
    auto *appearanceTab = new QWidget();
    auto *notificationTypesTab = new QWidget();
    auto *privacyTab = new QWidget();
    auto *voiceTab = new QWidget();
    auto *soundTab = new QWidget();
    auto *userSoundsTab = new QWidget();
    auto *nativeTab = new QWidget();
    auto *testTab = new QWidget();

    m_tabWidget->addTab(appearanceTab, tr("Appearance"));
    m_tabWidget->addTab(notificationTypesTab, tr("Notification Types"));
    m_tabWidget->addTab(privacyTab, tr("Privacy & Streaming"));
    m_tabWidget->addTab(voiceTab, tr("Voice"));
    m_tabWidget->addTab(soundTab, tr("Sounds"));
    m_tabWidget->addTab(userSoundsTab, tr("Per-User Sounds"));
    m_tabWidget->addTab(nativeTab, tr("Native Notifications"));
    m_tabWidget->addTab(testTab, tr("Test & Preview"));

    setupAppearanceTab(appearanceTab);
    setupNotificationTypesTab(notificationTypesTab);
    setupPrivacyTab(privacyTab);
    setupVoiceTab(voiceTab);
    setupSoundTab(soundTab);
    setupUserSoundsTab(userSoundsTab);
    setupNativeTab(nativeTab);
    setupTestTab(testTab);
}

void NotificationsPage::setupAppearanceTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    // General
    auto *generalGroup = new QGroupBox(tr("General"), content);
    auto *generalLayout = new QFormLayout(generalGroup);

    m_enabledCheck = new QCheckBox(tr("Enable notifications"), generalGroup);
    m_enabledCheck->setChecked(true);
    generalLayout->addRow(m_enabledCheck);

    m_deliveryCombo = new QComboBox(generalGroup);
    m_deliveryCombo->addItem(tr("In-app toasts"), "in-app");
    m_deliveryCombo->addItem(tr("OS native notifications"), "native");
    m_deliveryCombo->addItem(tr("Both"), "both");
    generalLayout->addRow(tr("Notification Style:"), m_deliveryCombo);

    m_groupingCheck = new QCheckBox(tr("Group multiple messages from the same conversation into one toast"), generalGroup);
    m_groupingCheck->setChecked(true);
    generalLayout->addRow(m_groupingCheck);

    contentLayout->addWidget(generalGroup);

    // Position
    auto *positionGroup = new QGroupBox(tr("Position & Layout"), content);
    auto *positionLayout = new QFormLayout(positionGroup);

    m_positionCombo = new QComboBox(positionGroup);
    m_positionCombo->addItem(tr("Bottom Left"), "bottom-left");
    m_positionCombo->addItem(tr("Top Left"), "top-left");
    m_positionCombo->addItem(tr("Top Right"), "top-right");
    m_positionCombo->addItem(tr("Bottom Right"), "bottom-right");
    m_positionCombo->addItem(tr("Center"), "center");
    positionLayout->addRow(tr("Screen Position:"), m_positionCombo);

    m_maxNotificationsSpin = new QSpinBox(positionGroup);
    m_maxNotificationsSpin->setRange(1, 10);
    m_maxNotificationsSpin->setValue(3);
    positionLayout->addRow(tr("Max Notifications:"), m_maxNotificationsSpin);

    m_timeoutSpin = new QSpinBox(positionGroup);
    m_timeoutSpin->setRange(1, 60);
    m_timeoutSpin->setSuffix(tr(" sec"));
    m_timeoutSpin->setValue(5);
    positionLayout->addRow(tr("Timeout:"), m_timeoutSpin);

    m_opacitySlider = new QSlider(Qt::Horizontal, positionGroup);
    m_opacitySlider->setRange(10, 100);
    m_opacitySlider->setValue(95);
    positionLayout->addRow(tr("Opacity:"), m_opacitySlider);

    m_edgeOffsetSpin = new QSpinBox(positionGroup);
    m_edgeOffsetSpin->setRange(0, 200);
    m_edgeOffsetSpin->setSuffix(tr(" px"));
    m_edgeOffsetSpin->setValue(20);
    positionLayout->addRow(tr("Edge Offset:"), m_edgeOffsetSpin);

    m_scaleSpin = new QDoubleSpinBox(positionGroup);
    m_scaleSpin->setRange(0.5, 2.0);
    m_scaleSpin->setSingleStep(0.1);
    m_scaleSpin->setValue(1.0);
    positionLayout->addRow(tr("Scale Factor:"), m_scaleSpin);

    m_pauseOnHoverCheck = new QCheckBox(tr("Pause timeout on hover"), positionGroup);
    m_pauseOnHoverCheck->setChecked(true);
    positionLayout->addRow(m_pauseOnHoverCheck);

    m_renderImagesCheck = new QCheckBox(tr("Show avatars and images"), positionGroup);
    m_renderImagesCheck->setChecked(true);
    positionLayout->addRow(m_renderImagesCheck);

    m_animationsCheck = new QCheckBox(tr("Animate toast entry, exit, and expansion"), positionGroup);
    m_animationsCheck->setChecked(true);
    positionLayout->addRow(m_animationsCheck);

    m_progressBarCheck = new QCheckBox(tr("Show countdown progress bar on toasts"), positionGroup);
    m_progressBarCheck->setChecked(true);
    positionLayout->addRow(m_progressBarCheck);

    m_coloredAccentsCheck = new QCheckBox(tr("Colored toasts (author and channel accents)"), positionGroup);
    m_coloredAccentsCheck->setChecked(true);
    positionLayout->addRow(m_coloredAccentsCheck);

    contentLayout->addWidget(positionGroup);
    contentLayout->addStretch();

    // Connect signals
    connect(m_enabledCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_deliveryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_groupingCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_positionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_maxNotificationsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_timeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_opacitySlider, &QSlider::valueChanged, this, &NotificationsPage::onSettingChanged);
    connect(m_edgeOffsetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_pauseOnHoverCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_renderImagesCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_animationsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_progressBarCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_coloredAccentsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
}

void NotificationsPage::setupNotificationTypesTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *group = new QGroupBox(tr("When to Notify"), content);
    auto *groupLayout = new QVBoxLayout(group);

    m_mentionsCheck = new QCheckBox(tr("Notify on @mentions"), group);
    m_mentionsCheck->setChecked(true);
    groupLayout->addWidget(m_mentionsCheck);

    m_directMessagesCheck = new QCheckBox(tr("Notify on direct messages"), group);
    m_directMessagesCheck->setChecked(true);
    groupLayout->addWidget(m_directMessagesCheck);

    m_groupMessagesCheck = new QCheckBox(tr("Notify on group chat messages"), group);
    m_groupMessagesCheck->setChecked(true);
    groupLayout->addWidget(m_groupMessagesCheck);

    m_friendServerMessagesCheck = new QCheckBox(tr("Notify on messages from friends in servers"), group);
    m_friendServerMessagesCheck->setChecked(true);
    groupLayout->addWidget(m_friendServerMessagesCheck);

    m_friendRequestsCheck = new QCheckBox(tr("Notify on friend requests and acceptances"), group);
    m_friendRequestsCheck->setChecked(true);
    groupLayout->addWidget(m_friendRequestsCheck);

    m_respectServerSettingsCheck = new QCheckBox(tr("Respect Discord's per-server notification settings"), group);
    m_respectServerSettingsCheck->setChecked(true);
    groupLayout->addWidget(m_respectServerSettingsCheck);

    contentLayout->addWidget(group);

    // Always Notify For list
    auto *notifyGroup = new QGroupBox(tr("Always Notify For"), content);
    auto *notifyLayout = new QVBoxLayout(notifyGroup);
    m_notifyForList = new QListWidget(notifyGroup);
    notifyLayout->addWidget(m_notifyForList);
    auto *notifyBtnLayout = new QHBoxLayout();
    auto *addNotifyBtn = new QPushButton(tr("Add..."), notifyGroup);
    auto *removeNotifyBtn = new QPushButton(tr("Remove"), notifyGroup);
    notifyBtnLayout->addWidget(addNotifyBtn);
    notifyBtnLayout->addWidget(removeNotifyBtn);
    notifyBtnLayout->addStretch();
    notifyLayout->addLayout(notifyBtnLayout);
    contentLayout->addWidget(notifyGroup);

    connect(addNotifyBtn, &QPushButton::clicked, this, &NotificationsPage::onAddNotifyFor);
    connect(removeNotifyBtn, &QPushButton::clicked, this, &NotificationsPage::onRemoveNotifyFor);

    // Ignore Users list
    auto *ignoreGroup = new QGroupBox(tr("Ignore Users"), content);
    auto *ignoreLayout = new QVBoxLayout(ignoreGroup);
    m_ignoreUsersList = new QListWidget(ignoreGroup);
    ignoreLayout->addWidget(m_ignoreUsersList);
    auto *ignoreBtnLayout = new QHBoxLayout();
    auto *addIgnoreBtn = new QPushButton(tr("Add..."), ignoreGroup);
    auto *removeIgnoreBtn = new QPushButton(tr("Remove"), ignoreGroup);
    ignoreBtnLayout->addWidget(addIgnoreBtn);
    ignoreBtnLayout->addWidget(removeIgnoreBtn);
    ignoreBtnLayout->addStretch();
    ignoreLayout->addLayout(ignoreBtnLayout);
    contentLayout->addWidget(ignoreGroup);

    connect(addIgnoreBtn, &QPushButton::clicked, this, &NotificationsPage::onAddIgnoreUser);
    connect(removeIgnoreBtn, &QPushButton::clicked, this, &NotificationsPage::onRemoveIgnoreUser);

    contentLayout->addStretch();

    connect(m_mentionsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_directMessagesCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_groupMessagesCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_friendServerMessagesCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_friendRequestsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_respectServerSettingsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
}

void NotificationsPage::setupPrivacyTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *group = new QGroupBox(tr("Streamer Mode"), content);
    auto *groupLayout = new QFormLayout(group);

    m_disableStreamerModeCheck = new QCheckBox(tr("Disable notifications while in Streamer Mode"), group);
    m_disableStreamerModeCheck->setChecked(true);
    groupLayout->addRow(m_disableStreamerModeCheck);

    m_streamingTreatmentCombo = new QComboBox(group);
    m_streamingTreatmentCombo->addItem(tr("Normal"), "normal");
    m_streamingTreatmentCombo->addItem(tr("Hide Content (show notification but no message text)"), "no-content");
    m_streamingTreatmentCombo->addItem(tr("Ignore All (no notifications while streaming)"), "ignore");
    groupLayout->addRow(tr("While Screen Sharing:"), m_streamingTreatmentCombo);

    contentLayout->addWidget(group);
    contentLayout->addStretch();

    connect(m_disableStreamerModeCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_streamingTreatmentCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NotificationsPage::onSettingChanged);
}

void NotificationsPage::setupVoiceTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *group = new QGroupBox(tr("Voice Channel Notifications"), content);
    auto *groupLayout = new QFormLayout(group);

    m_voiceJoinsCheck = new QCheckBox(tr("Notify when watched users join/leave voice channels"), group);
    m_voiceJoinsCheck->setChecked(false);
    groupLayout->addRow(m_voiceJoinsCheck);

    m_voiceDebounceSpin = new QSpinBox(group);
    m_voiceDebounceSpin->setRange(500, 30000);
    m_voiceDebounceSpin->setSingleStep(500);
    m_voiceDebounceSpin->setSuffix(tr(" ms"));
    m_voiceDebounceSpin->setValue(2000);
    groupLayout->addRow(tr("Minimum time between voice notifications:"), m_voiceDebounceSpin);

    contentLayout->addWidget(group);
    contentLayout->addStretch();

    connect(m_voiceJoinsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_voiceDebounceSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &NotificationsPage::onSettingChanged);
}

void NotificationsPage::setupSoundTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    // Global volume
    auto *volumeGroup = new QGroupBox(tr("Global Volume"), content);
    auto *volumeLayout = new QFormLayout(volumeGroup);

    m_globalVolumeSlider = new QSlider(Qt::Horizontal, volumeGroup);
    m_globalVolumeSlider->setRange(0, 100);
    m_globalVolumeSlider->setValue(100);
    volumeLayout->addRow(tr("Notification Sound Volume:"), m_globalVolumeSlider);

    contentLayout->addWidget(volumeGroup);

    auto *typeGroup = new QGroupBox(tr("Play sound for"), content);
    auto *typeLayout = new QVBoxLayout(typeGroup);
    m_soundDMsCheck = new QCheckBox(tr("Direct messages"), typeGroup);
    m_soundGroupDMsCheck = new QCheckBox(tr("Group DMs"), typeGroup);
    m_soundMentionsCheck = new QCheckBox(tr("@mentions"), typeGroup);
    m_soundFriendServerCheck = new QCheckBox(tr("Friends in servers"), typeGroup);
    m_soundFriendRequestsCheck = new QCheckBox(tr("Friend requests / acceptances"), typeGroup);
    typeLayout->addWidget(m_soundDMsCheck);
    typeLayout->addWidget(m_soundGroupDMsCheck);
    typeLayout->addWidget(m_soundMentionsCheck);
    typeLayout->addWidget(m_soundFriendServerCheck);
    typeLayout->addWidget(m_soundFriendRequestsCheck);
    contentLayout->addWidget(typeGroup);

    // Sound overrides
    auto *overridesGroup = new QGroupBox(tr("Sound Overrides"), content);
    auto *overridesLayout = new QVBoxLayout(overridesGroup);

    m_soundOverridesList = new QListWidget(overridesGroup);
    m_soundOverridesList->setSelectionMode(QAbstractItemView::SingleSelection);

    // Default sound types with full editor
    QStringList soundTypes = {
        "notification_default",
        "message1",
        "message2",
        "message3",
        "mention1",
        "mention2",
        "mention3"
    };

    QStringList soundTypeLabels = {
        tr("Default Notification"),
        tr("Message 1 (Generic)"),
        tr("Message 2 (Reply in Server)"),
        tr("Message 3 (DMs & Group DMs)"),
        tr("Mention 1 (@role)"),
        tr("Mention 2 (@everyone)"),
        tr("Mention 3 (@here)")
    };

    for (int i = 0; i < soundTypes.size(); ++i) {
        auto *item = new QListWidgetItem(m_soundOverridesList);
        item->setData(Qt::UserRole, soundTypes[i]);
        
        auto *widget = new SoundOverrideWidget(soundTypes[i], soundTypeLabels[i], m_soundOverridesList);
        m_soundOverridesList->setItemWidget(item, widget);
        item->setSizeHint(widget->sizeHint());
    }

    overridesLayout->addWidget(m_soundOverridesList);

    auto *buttonLayout = new QHBoxLayout();
    m_importSoundsBtn = new QPushButton(tr("Import"), overridesGroup);
    m_exportSoundsBtn = new QPushButton(tr("Export"), overridesGroup);
    m_resetSoundsBtn = new QPushButton(tr("Reset All"), overridesGroup);
    m_resetSoundsBtn->setStyleSheet("color: #ff6b6b;");
    buttonLayout->addWidget(m_importSoundsBtn);
    buttonLayout->addWidget(m_exportSoundsBtn);
    buttonLayout->addWidget(m_resetSoundsBtn);
    buttonLayout->addStretch();
    overridesLayout->addLayout(buttonLayout);

    contentLayout->addWidget(overridesGroup);
    contentLayout->addStretch();

    connect(m_globalVolumeSlider, &QSlider::valueChanged, this, &NotificationsPage::onSettingChanged);
    connect(m_soundDMsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_soundGroupDMsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_soundMentionsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_soundFriendServerCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_soundFriendRequestsCheck, &QCheckBox::toggled, this, &NotificationsPage::onSettingChanged);
    connect(m_importSoundsBtn, &QPushButton::clicked, this, &NotificationsPage::onImportSettings);
    connect(m_exportSoundsBtn, &QPushButton::clicked, this, &NotificationsPage::onExportSettings);
    connect(m_resetSoundsBtn, &QPushButton::clicked, this, &NotificationsPage::onResetSounds);
}

void NotificationsPage::setupUserSoundsTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *infoLabel = new QLabel(tr("Configure custom notification sounds for specific users. Right-click a user in Discord to set their sound."), content);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #72767d; font-style: italic; margin-bottom: 16px;");
    contentLayout->addWidget(infoLabel);

    m_userSoundsList = new QListWidget(content);
    m_userSoundsList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_userSoundsList->setContextMenuPolicy(Qt::CustomContextMenu);
    contentLayout->addWidget(m_userSoundsList);

    auto *buttonLayout = new QHBoxLayout();
    m_clearUserSoundsBtn = new QPushButton(tr("Clear All User Sounds"), content);
    m_clearUserSoundsBtn->setStyleSheet("color: #ff6b6b;");
    buttonLayout->addWidget(m_clearUserSoundsBtn);
    buttonLayout->addStretch();
    contentLayout->addLayout(buttonLayout);

    contentLayout->addStretch();

    connect(m_clearUserSoundsBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, tr("Clear All"), tr("Remove all per-user sound settings?")) == QMessageBox::Yes) {
            m_userSoundsList->clear();
            onSettingChanged();
        }
    });

    connect(m_userSoundsList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_userSoundsList->itemAt(pos);
        if (!item) return;

        QMenu menu(this);
        auto *removeAction = menu.addAction(tr("Remove"));
        connect(removeAction, &QAction::triggered, this, [this, item]() {
            delete m_userSoundsList->takeItem(m_userSoundsList->row(item));
            onSettingChanged();
        });
        menu.exec(m_userSoundsList->mapToGlobal(pos));
    });
}

void NotificationsPage::setupNativeTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *group = new QGroupBox(tr("Native OS Notifications"), content);
    auto *groupLayout = new QFormLayout(group);

    m_nativeModeCombo = new QComboBox(group);
    m_nativeModeCombo->addItem(tr("Never"), "never");
    m_nativeModeCombo->addItem(tr("Always"), "always");
    m_nativeModeCombo->addItem(tr("When Discord is not focused"), "not-focused");
    groupLayout->addRow(tr("Use native notifications:"), m_nativeModeCombo);

    m_requestPermissionBtn = new QPushButton(tr("Request Permission"), group);
    groupLayout->addRow(tr("Permission:"), m_requestPermissionBtn);

    auto *noteLabel = new QLabel(tr("Native notifications use your operating system notification tray. This policy applies when the notification style (Appearance tab) is set to 'Both'. Click the button above to send the permission/test notification."), group);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #72767d; font-size: 12px;");
    groupLayout->addRow(noteLabel);

    contentLayout->addWidget(group);
    contentLayout->addStretch();

    connect(m_nativeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &NotificationsPage::onSettingChanged);
    connect(m_requestPermissionBtn, &QPushButton::clicked, this, &NotificationsPage::onRequestNativePermission);
}

void NotificationsPage::setupTestTab(QWidget *tab)
{
    auto *layout = new QVBoxLayout(tab);
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget();
    auto *contentLayout = new QVBoxLayout(content);
    scroll->setWidget(content);
    layout->addWidget(scroll);

    auto *group = new QGroupBox(tr("Test Notifications"), content);
    auto *groupLayout = new QVBoxLayout(group);

    m_testNotificationBtn = new QPushButton(tr("Send Test Notification"), group);
    m_testNotificationBtn->setMinimumHeight(40);
    groupLayout->addWidget(m_testNotificationBtn);

    m_dismissAllBtn = new QPushButton(tr("Dismiss All Notifications"), group);
    m_dismissAllBtn->setMinimumHeight(40);
    m_dismissAllBtn->setStyleSheet("color: #ff6b6b;");
    groupLayout->addWidget(m_dismissAllBtn);

    auto *noteLabel = new QLabel(tr("Click 'Send Test Notification' to preview how notifications will look and sound."), group);
    noteLabel->setWordWrap(true);
    noteLabel->setStyleSheet("color: #72767d; margin-top: 8px;");
    groupLayout->addWidget(noteLabel);

    contentLayout->addWidget(group);
    contentLayout->addStretch();

    connect(m_testNotificationBtn, &QPushButton::clicked, this, &NotificationsPage::onSendTestNotification);
    connect(m_dismissAllBtn, &QPushButton::clicked, this, &NotificationsPage::onDismissAllNotifications);
}

void NotificationsPage::loadSettings()
{
    m_loadingSettings = true;
    QSettings settings;

    // General
    m_enabledCheck->setChecked(settings.value("notifications/enabled", true).toBool());
    int deliveryIdx = m_deliveryCombo->findData(settings.value("notifications/delivery", "in-app").toString());
    if (deliveryIdx >= 0) m_deliveryCombo->setCurrentIndex(deliveryIdx);
    m_groupingCheck->setChecked(settings.value("notifications/grouping", true).toBool());

    // Appearance
    QString pos = settings.value("notifications/position", "bottom-left").toString();
    int idx = m_positionCombo->findData(pos);
    if (idx >= 0) m_positionCombo->setCurrentIndex(idx);

    m_maxNotificationsSpin->setValue(settings.value("notifications/max_notifications", 3).toInt());
    m_timeoutSpin->setValue(settings.value("notifications/timeout", 5).toInt());
    m_opacitySlider->setValue(settings.value("notifications/opacity", 95).toInt());
    m_edgeOffsetSpin->setValue(settings.value("notifications/edge_offset", 20).toInt());
    m_scaleSpin->setValue(settings.value("notifications/scale", 1.0).toDouble());
    m_pauseOnHoverCheck->setChecked(settings.value("notifications/pause_on_hover", true).toBool());
    m_renderImagesCheck->setChecked(settings.value("notifications/render_images", true).toBool());
    m_animationsCheck->setChecked(settings.value("notifications/animations", true).toBool());
    m_progressBarCheck->setChecked(settings.value("notifications/progress_bar", true).toBool());
    m_coloredAccentsCheck->setChecked(settings.value("notifications/colored_accents", true).toBool());

    // Notification Types
    m_mentionsCheck->setChecked(settings.value("notifications/mentions", true).toBool());
    m_directMessagesCheck->setChecked(settings.value("notifications/direct_messages", true).toBool());
    m_groupMessagesCheck->setChecked(settings.value("notifications/group_messages", true).toBool());
    m_friendServerMessagesCheck->setChecked(settings.value("notifications/friend_server_messages", true).toBool());
    m_friendRequestsCheck->setChecked(settings.value("notifications/friend_requests", true).toBool());
    m_respectServerSettingsCheck->setChecked(settings.value("notifications/respect_server_settings", true).toBool());

    // Privacy
    m_disableStreamerModeCheck->setChecked(settings.value("notifications/disable_streamer_mode", true).toBool());
    QString streaming = settings.value("notifications/streaming_treatment", "normal").toString();
    idx = m_streamingTreatmentCombo->findData(streaming);
    if (idx >= 0) m_streamingTreatmentCombo->setCurrentIndex(idx);

    // Voice
    m_voiceJoinsCheck->setChecked(settings.value("notifications/voice_joins", false).toBool());
    m_voiceDebounceSpin->setValue(settings.value("notifications/voice_debounce", 2000).toInt());

    // Sound
    m_globalVolumeSlider->setValue(settings.value("notifications/sound_volume", 100).toInt());
    m_soundDMsCheck->setChecked(settings.value("notifications/sound_for_dms", true).toBool());
    m_soundGroupDMsCheck->setChecked(settings.value("notifications/sound_for_group_dms", true).toBool());
    m_soundMentionsCheck->setChecked(settings.value("notifications/sound_for_mentions", true).toBool());
    m_soundFriendServerCheck->setChecked(settings.value("notifications/sound_for_friend_server_messages", true).toBool());
    m_soundFriendRequestsCheck->setChecked(settings.value("notifications/sound_for_friend_requests", true).toBool());

    // Sound overrides
    QVariant overridesVar = settings.value("notifications/sound_overrides");
    if (overridesVar.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(overridesVar.toByteArray());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            for (int i = 0; i < m_soundOverridesList->count(); ++i) {
                auto *item = m_soundOverridesList->item(i);
                QString soundId = item->data(Qt::UserRole).toString();
                if (obj.contains(soundId)) {
                    QJsonObject o = obj[soundId].toObject();
                    // Get the SoundOverrideWidget and load from JSON
                    auto *widget = qobject_cast<SoundOverrideWidget *>(m_soundOverridesList->itemWidget(item));
                    if (widget) {
                        widget->loadFromJson(o);
                    }
                }
            }
        }
    }

    // User sounds
    QVariant userSoundsVar = settings.value("notifications/user_sounds");
    if (userSoundsVar.isValid()) {
        QJsonDocument doc = QJsonDocument::fromJson(userSoundsVar.toByteArray());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            m_userSoundsList->clear();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                QJsonObject o = it.value().toObject();
                auto *item = new QListWidgetItem(
                    QString("%1 (%2)").arg(it.key(), o["selected_sound"].toString()),
                    m_userSoundsList);
                item->setData(Qt::UserRole, it.key());
                item->setData(Qt::UserRole + 1, o);
            }
        }
    }

    // Native
    QString nativeMode = settings.value("notifications/native_mode", "not-focused").toString();
    int nativeIdx = m_nativeModeCombo->findData(nativeMode);
    if (nativeIdx >= 0) m_nativeModeCombo->setCurrentIndex(nativeIdx);

    m_loadingSettings = false;
}

void NotificationsPage::saveSettings()
{
    QSettings settings;

    // General
    settings.setValue("notifications/enabled", m_enabledCheck->isChecked());
    settings.setValue("notifications/delivery", m_deliveryCombo->currentData());
    settings.setValue("notifications/grouping", m_groupingCheck->isChecked());

    // Appearance
    settings.setValue("notifications/position", m_positionCombo->currentData());
    settings.setValue("notifications/max_notifications", m_maxNotificationsSpin->value());
    settings.setValue("notifications/timeout", m_timeoutSpin->value());
    settings.setValue("notifications/opacity", m_opacitySlider->value());
    settings.setValue("notifications/edge_offset", m_edgeOffsetSpin->value());
    settings.setValue("notifications/scale", m_scaleSpin->value());
    settings.setValue("notifications/pause_on_hover", m_pauseOnHoverCheck->isChecked());
    settings.setValue("notifications/render_images", m_renderImagesCheck->isChecked());
    settings.setValue("notifications/animations", m_animationsCheck->isChecked());
    settings.setValue("notifications/progress_bar", m_progressBarCheck->isChecked());
    settings.setValue("notifications/colored_accents", m_coloredAccentsCheck->isChecked());

    // Notification Types
    settings.setValue("notifications/mentions", m_mentionsCheck->isChecked());
    settings.setValue("notifications/direct_messages", m_directMessagesCheck->isChecked());
    settings.setValue("notifications/group_messages", m_groupMessagesCheck->isChecked());
    settings.setValue("notifications/friend_server_messages", m_friendServerMessagesCheck->isChecked());
    settings.setValue("notifications/friend_requests", m_friendRequestsCheck->isChecked());
    settings.setValue("notifications/respect_server_settings", m_respectServerSettingsCheck->isChecked());

    // Privacy
    settings.setValue("notifications/disable_streamer_mode", m_disableStreamerModeCheck->isChecked());
    settings.setValue("notifications/streaming_treatment", m_streamingTreatmentCombo->currentData());

    // Voice
    settings.setValue("notifications/voice_joins", m_voiceJoinsCheck->isChecked());
    settings.setValue("notifications/voice_debounce", m_voiceDebounceSpin->value());

    // Sound
    settings.setValue("notifications/sound_volume", m_globalVolumeSlider->value());
    settings.setValue("notifications/sound_for_dms", m_soundDMsCheck->isChecked());
    settings.setValue("notifications/sound_for_group_dms", m_soundGroupDMsCheck->isChecked());
    settings.setValue("notifications/sound_for_mentions", m_soundMentionsCheck->isChecked());
    settings.setValue("notifications/sound_for_friend_server_messages", m_soundFriendServerCheck->isChecked());
    settings.setValue("notifications/sound_for_friend_requests", m_soundFriendRequestsCheck->isChecked());

    // Sound overrides
    QJsonObject overridesObj;
    for (int i = 0; i < m_soundOverridesList->count(); ++i) {
        auto *item = m_soundOverridesList->item(i);
        QString soundId = item->data(Qt::UserRole).toString();
        auto *widget = qobject_cast<SoundOverrideWidget *>(m_soundOverridesList->itemWidget(item));
        if (widget) {
            QJsonObject o = widget->toJson();
            item->setData(Qt::UserRole + 1, o);
            overridesObj[soundId] = o;
        }
    }
    settings.setValue("notifications/sound_overrides", QJsonDocument(overridesObj).toJson(QJsonDocument::Compact));

    // User sounds
    QJsonObject userSoundsObj;
    for (int i = 0; i < m_userSoundsList->count(); ++i) {
        auto *item = m_userSoundsList->item(i);
        QString userId = item->data(Qt::UserRole).toString();
        QJsonObject o = item->data(Qt::UserRole + 1).toJsonObject();
        userSoundsObj[userId] = o;
    }
    settings.setValue("notifications/user_sounds", QJsonDocument(userSoundsObj).toJson(QJsonDocument::Compact));

    // Native
    settings.setValue("notifications/native_mode", m_nativeModeCombo->currentData());

    settings.sync();
}

void NotificationsPage::onSettingChanged()
{
    if (m_loadingSettings)
        return;
    saveSettings();
    emit settingsChanged();
}

void NotificationsPage::setNotificationManager(Core::NotificationManager *mgr)
{
    m_notificationManager = mgr;
    refreshNotifyLists();
}

void NotificationsPage::refreshNotifyLists()
{
    if (!m_notificationManager)
        return;

    m_notifyForList->clear();
    const auto notify = m_notificationManager->notifyForList();
    for (const auto &id : notify)
        m_notifyForList->addItem(id);

    m_ignoreUsersList->clear();
    const auto ignore = m_notificationManager->ignoreUsersList();
    for (const auto &id : ignore)
        m_ignoreUsersList->addItem(id);
}

void NotificationsPage::onAddNotifyFor()
{
    if (!m_notificationManager)
        return;
    bool ok;
    QString id = QInputDialog::getText(this, tr("Add to Notify List"), tr("Enter User/Channel/Guild ID:"), QLineEdit::Normal, "", &ok);
    if (ok && !id.isEmpty()) {
        m_notificationManager->addToNotifyList(id);
        refreshNotifyLists();
    }
}

void NotificationsPage::onRemoveNotifyFor()
{
    if (!m_notificationManager)
        return;
    QString id;
    if (m_notifyForList->currentItem()) {
        id = m_notifyForList->currentItem()->text();
    } else {
        bool ok;
        id = QInputDialog::getText(this, tr("Remove from Notify List"), tr("Enter User/Channel/Guild ID:"), QLineEdit::Normal, "", &ok);
        if (!ok)
            return;
    }
    if (!id.isEmpty()) {
        m_notificationManager->removeFromNotifyList(id);
        refreshNotifyLists();
    }
}

void NotificationsPage::onAddIgnoreUser()
{
    if (!m_notificationManager)
        return;
    bool ok;
    QString id = QInputDialog::getText(this, tr("Add to Ignore List"), tr("Enter User ID:"), QLineEdit::Normal, "", &ok);
    if (ok && !id.isEmpty()) {
        m_notificationManager->addToIgnoreList(id);
        refreshNotifyLists();
    }
}

void NotificationsPage::onRemoveIgnoreUser()
{
    if (!m_notificationManager)
        return;
    QString id;
    if (m_ignoreUsersList->currentItem()) {
        id = m_ignoreUsersList->currentItem()->text();
    } else {
        bool ok;
        id = QInputDialog::getText(this, tr("Remove from Ignore List"), tr("Enter User ID:"), QLineEdit::Normal, "", &ok);
        if (!ok)
            return;
    }
    if (!id.isEmpty()) {
        m_notificationManager->removeFromIgnoreList(id);
        refreshNotifyLists();
    }
}

void NotificationsPage::onImportSettings()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Import Notification Settings"), "", "JSON Files (*.json)");
    if (file.isEmpty()) return;

    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Error"), tr("Failed to open file."));
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("Error"), tr("Invalid JSON file."));
        return;
    }

    QJsonObject obj = doc.object();

    // Import sound overrides
    if (obj.contains("overrides") && obj["overrides"].isArray()) {
        QJsonArray arr = obj["overrides"].toArray();
        for (const QJsonValue &val : arr) {
            QJsonObject o = val.toObject();
            QString id = o["id"].toString();
            for (int i = 0; i < m_soundOverridesList->count(); ++i) {
                auto *item = m_soundOverridesList->item(i);
                if (item->data(Qt::UserRole).toString() == id) {
                    item->setCheckState(o["enabled"].toBool() ? Qt::Checked : Qt::Unchecked);
                    item->setData(Qt::UserRole + 1, o);
                    auto *widget = qobject_cast<SoundOverrideWidget *>(m_soundOverridesList->itemWidget(item));
                    if (widget)
                        widget->loadFromJson(o);
                    break;
                }
            }
        }
    }

    // Import user sounds
    if (obj.contains("userSounds") && obj["userSounds"].isObject()) {
        m_userSoundsList->clear();
        QJsonObject userSounds = obj["userSounds"].toObject();
        for (auto it = userSounds.begin(); it != userSounds.end(); ++it) {
            QJsonObject o = it.value().toObject();
            auto *item = new QListWidgetItem(QString("%1 (%2)").arg(it.key()).arg(o["selected_sound"].toString()), m_userSoundsList);
            item->setData(Qt::UserRole, it.key());
            item->setData(Qt::UserRole + 1, o);
        }
    }

    onSettingChanged();
    QMessageBox::information(this, tr("Success"), tr("Settings imported successfully!"));
}

void NotificationsPage::onExportSettings()
{
    QString file = QFileDialog::getSaveFileName(this, tr("Export Notification Settings"), "notification-settings.json", "JSON Files (*.json)");
    if (file.isEmpty()) return;

    QJsonObject obj;

    QJsonArray overridesArr;
    for (int i = 0; i < m_soundOverridesList->count(); ++i) {
        auto *item = m_soundOverridesList->item(i);
        QJsonObject o;
        auto *widget = qobject_cast<SoundOverrideWidget *>(m_soundOverridesList->itemWidget(item));
        if (widget) {
            o = widget->toJson();
        } else {
            o = item->data(Qt::UserRole + 1).toJsonObject();
        }
        o["enabled"] = o.value("enabled").toBool(item->checkState() == Qt::Checked);
        o["id"] = item->data(Qt::UserRole).toString();
        overridesArr.append(o);
    }
    obj["overrides"] = overridesArr;

    QJsonObject userSoundsObj;
    for (int i = 0; i < m_userSoundsList->count(); ++i) {
        auto *item = m_userSoundsList->item(i);
        userSoundsObj[item->data(Qt::UserRole).toString()] = item->data(Qt::UserRole + 1).toJsonObject();
    }
    obj["userSounds"] = userSoundsObj;

    obj["__note"] = "Audio files must be re-uploaded after import";

    QFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        f.close();
        QMessageBox::information(this, tr("Success"), tr("Exported %1 sound settings.").arg(overridesArr.size()));
    } else {
        QMessageBox::warning(this, tr("Error"), tr("Failed to write file."));
    }
}

void NotificationsPage::onResetSounds()
{
    if (QMessageBox::question(this, tr("Reset All Sounds"), tr("Reset all sound overrides to defaults?")) == QMessageBox::Yes) {
        for (int i = 0; i < m_soundOverridesList->count(); ++i) {
            auto *item = m_soundOverridesList->item(i);
            item->setCheckState(Qt::Unchecked);
            item->setData(Qt::UserRole + 1, QJsonObject());
        }
        onSettingChanged();
    }
}

void NotificationsPage::onSendTestNotification()
{
    if (!m_notificationManager) {
        QMessageBox::warning(this,
                             tr("Notifications unavailable"),
                             tr("Notification manager is not ready yet. Connect an account, then try the test notification again."));
        return;
    }

    m_notificationManager->sendTestNotification();
}

void NotificationsPage::onDismissAllNotifications()
{
    if (m_notificationManager)
        m_notificationManager->dismissAllNotifications();
}

void NotificationsPage::onRequestNativePermission()
{
    QString errorMessage;
    if (m_notificationManager) {
        if (m_notificationManager->requestNativeNotificationPermission(&errorMessage)) {
            QMessageBox::information(this,
                                     tr("Permission requested"),
                                     tr("A native notification was sent. If you do not see it, check your system notification settings for Acheron."));
        } else {
            QMessageBox::warning(this,
                                 tr("Native notifications unavailable"),
                                 errorMessage);
        }
        return;
    }

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        QMessageBox::warning(this,
                             tr("Native notifications unavailable"),
                             tr("The operating system notification tray is not available."));
        return;
    }

    if (!QSystemTrayIcon::supportsMessages()) {
        QMessageBox::warning(this,
                             tr("Native notifications unavailable"),
                             tr("This desktop environment does not support tray notifications."));
        return;
    }

    if (!m_permissionTray) {
        m_permissionTray = new QSystemTrayIcon(qApp->windowIcon(), this);
        m_permissionTray->setToolTip(tr("Acheron"));
    }

    if (!m_permissionTray->isVisible())
        m_permissionTray->setVisible(true);

    m_permissionTray->showMessage(tr("Acheron notifications enabled"),
                                  tr("Native notification permission test sent."),
                                  QSystemTrayIcon::Information,
                                  5000);
    QMessageBox::information(this,
                             tr("Permission requested"),
                             tr("A native notification was sent. If you do not see it, check your system notification settings for Acheron."));
}

} // namespace UI
} // namespace Acheron

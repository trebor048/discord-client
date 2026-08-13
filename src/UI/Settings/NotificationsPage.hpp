#pragma once

#include <QWidget>
#include <QTabWidget>

class QCheckBox;
class QSlider;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QPushButton;
class QLineEdit;
class QListWidget;
class QGroupBox;
class QScrollArea;
class QSystemTrayIcon;

namespace Acheron {
namespace Core {
class NotificationManager;
}
namespace UI {

class NotificationsPage : public QWidget
{
    Q_OBJECT
public:
    explicit NotificationsPage(QWidget *parent = nullptr);

    void setNotificationManager(Core::NotificationManager *mgr);

signals:
    void settingsChanged();

private slots:
    void onSettingChanged();
    void onAddNotifyFor();
    void onRemoveNotifyFor();
    void onAddIgnoreUser();
    void onRemoveIgnoreUser();
    void onImportSettings();
    void onExportSettings();
    void onResetSounds();
    void onSendTestNotification();
    void onDismissAllNotifications();
    void onRequestNativePermission();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void setupAppearanceTab(QWidget *tab);
    void setupNotificationTypesTab(QWidget *tab);
    void setupPrivacyTab(QWidget *tab);
    void setupVoiceTab(QWidget *tab);
    void setupSoundTab(QWidget *tab);
    void setupUserSoundsTab(QWidget *tab);
    void setupNativeTab(QWidget *tab);
    void setupTestTab(QWidget *tab);
    void refreshNotifyLists();

    QTabWidget *m_tabWidget = nullptr;

    // Appearance
    QComboBox *m_positionCombo = nullptr;
    QSpinBox *m_maxNotificationsSpin = nullptr;
    QSpinBox *m_timeoutSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QSpinBox *m_edgeOffsetSpin = nullptr;
    QDoubleSpinBox *m_scaleSpin = nullptr;
    QCheckBox *m_pauseOnHoverCheck = nullptr;
    QCheckBox *m_renderImagesCheck = nullptr;

    // Notification Types
    QCheckBox *m_mentionsCheck = nullptr;
    QCheckBox *m_directMessagesCheck = nullptr;
    QCheckBox *m_groupMessagesCheck = nullptr;
    QCheckBox *m_friendServerMessagesCheck = nullptr;
    QCheckBox *m_friendRequestsCheck = nullptr;
    QCheckBox *m_respectServerSettingsCheck = nullptr;

    // Privacy
    QCheckBox *m_disableStreamerModeCheck = nullptr;
    QComboBox *m_streamingTreatmentCombo = nullptr;

    // Voice
    QCheckBox *m_voiceJoinsCheck = nullptr;
    QSpinBox *m_voiceDebounceSpin = nullptr;

    // Sound
    QSlider *m_globalVolumeSlider = nullptr;
    QCheckBox *m_soundDMsCheck = nullptr;
    QCheckBox *m_soundGroupDMsCheck = nullptr;
    QCheckBox *m_soundMentionsCheck = nullptr;
    QCheckBox *m_soundFriendServerCheck = nullptr;
    QCheckBox *m_soundFriendRequestsCheck = nullptr;
    QListWidget *m_soundOverridesList = nullptr;
    QPushButton *m_importSoundsBtn = nullptr;
    QPushButton *m_exportSoundsBtn = nullptr;
    QPushButton *m_resetSoundsBtn = nullptr;

    // User Sounds
    QListWidget *m_userSoundsList = nullptr;
    QPushButton *m_clearUserSoundsBtn = nullptr;

    // Native
    QComboBox *m_nativeModeCombo = nullptr;
    QPushButton *m_requestPermissionBtn = nullptr;
    QSystemTrayIcon *m_permissionTray = nullptr;

    // Test
    QPushButton *m_testNotificationBtn = nullptr;
    QPushButton *m_dismissAllBtn = nullptr;

    Core::NotificationManager *m_notificationManager = nullptr;
    QListWidget *m_notifyForList = nullptr;
    QListWidget *m_ignoreUsersList = nullptr;
};

} // namespace UI
} // namespace Acheron

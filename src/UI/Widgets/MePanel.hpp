#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QToolButton;

namespace Acheron {
namespace Core {
class ClientInstance;
class ImageManager;
} // namespace Core
namespace UI {

// Bottom-left "me" panel: shows the current account's avatar + name and a
// presence picker (Online / Idle / Do Not Disturb / Invisible) plus a settings
// shortcut menu (jump to any settings page, quick toggles).
class MePanel : public QWidget
{
    Q_OBJECT
public:
    explicit MePanel(QWidget *parent = nullptr);

    void setInstance(Core::ClientInstance *instance, Core::ImageManager *images);
    void refresh();

signals:
    /// Ask the shell to open the Settings window on a specific page (matches a
    /// SettingsWindow category label, e.g. "Appearance").
    void openSettingsPageRequested(const QString &page);
    void compactModeChanged(bool enabled);
    void showTimestampsChanged(bool enabled);
    void compactInputChanged(bool enabled);
    void streamerModeChanged(bool enabled);
    void notificationSoundsChanged(bool enabled);

private:
    void openStatusMenu();
    void openSettingsMenu();

    QPointer<Core::ClientInstance> m_instance;
    Core::ImageManager *m_images = nullptr;
    bool avatarFetchWired_ = false;

    QLabel *avatarLabel = nullptr;
    QLabel *nameLabel = nullptr;
    QLabel *statusDot = nullptr;
    QToolButton *statusButton = nullptr;
    QToolButton *settingsButton = nullptr;
};

} // namespace UI
} // namespace Acheron

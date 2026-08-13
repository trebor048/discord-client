#pragma once

#include <QObject>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
class NotificationManager;
} // namespace Core
namespace UI {

class MainWindow;
class SettingsWindow;

// Owns the NotificationManager lifecycle and wiring that used to live in
// MainWindow: creating/tearing down the per-instance manager, connecting its
// navigation signals, keeping the active channel current, and feeding
// settings (streamer mode, reloads) into it. The manager itself reads the
// instance's presence status, so DND suppression (U9) flows through here.
class NotificationController : public QObject
{
    Q_OBJECT
public:
    explicit NotificationController(MainWindow *window);

    void teardown();
    void setupForInstance(Core::ClientInstance *instance);
    void setActiveChannel(Core::Snowflake channelId);
    void setStreamerModeEnabled(bool enabled);
    void reloadSettings();
    void applyToSettingsWindow(SettingsWindow *settingsWindow);

    Core::NotificationManager *manager() const { return notificationManager; }

private:
    MainWindow *m_window = nullptr;
    Core::NotificationManager *notificationManager = nullptr;

    friend class MainWindow;
};

} // namespace UI
} // namespace Acheron

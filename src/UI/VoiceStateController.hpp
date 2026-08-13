#pragma once

#include <QObject>
#include <QModelIndex>
#include <QString>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
namespace AV {
class PushToTalkListener;
} // namespace AV
} // namespace Core
namespace UI {

class MainWindow;

// Owns voice-state handling: the voice status-bar refresh, the push-to-talk
// listener and its key gating, and the join/disconnect wiring that used to
// live in MainWindow. The VoiceStatusBar widget itself remains owned by
// MainWindow (it is part of the layout); this controller drives it.
class VoiceStateController : public QObject
{
    Q_OBJECT
public:
    explicit VoiceStateController(MainWindow *window);

    // Push-to-talk listener lifetime mirrors the old MainWindow member.
    void createPushToTalkListener();

    // The voice portion of setupPermanentConnections.
    void connectInstanceVoice(Core::ClientInstance *instance);

    void updateVoiceStatusLabel();

    void setPushToTalkEnabledForAll(bool enabled);
    void setPushToTalkKey(const QString &key);

    // VoiceStatusBar "disconnect" action: leave whichever account is in voice.
    void disconnectActiveVoice();

    // channel tree join/disconnect handlers.
    void joinVoiceChannel(const QModelIndex &proxyIndex);
    void disconnectVoiceChannel(const QModelIndex &proxyIndex);

private:
    MainWindow *m_window = nullptr;
    Core::AV::PushToTalkListener *pushToTalkListener = nullptr;

    friend class MainWindow;
};

} // namespace UI
} // namespace Acheron

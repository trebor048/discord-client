#pragma once

#include <QAbstractNativeEventFilter>
#include <QEvent>
#include <QKeySequence>
#include <QObject>
#include <QTimer>

namespace Acheron {
namespace Core {
namespace AV {

// Detects whether the push-to-talk key is currently held. Installs itself as
// an application-level event filter so it sees key events regardless of which
// widget has focus (while the application is focused). On Windows it also
// registers the key as a system-wide hotkey so push-to-talk keeps working even
// while another application has focus. Emits pushToTalkKeyHeld on the
// press/release edges so VoiceManager can gate the transmit path.
class PushToTalkListener : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit PushToTalkListener(QObject *parent = nullptr);
    ~PushToTalkListener() override;

    void setKey(const QString &key);
    void setPushToTalkEnabled(bool enabled);

    [[nodiscard]] bool isHeld() const { return held; }

signals:
    void pushToTalkKeyHeld(bool held);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void applyKey(const QString &key);
    void setHeld(bool isHeld);
    void refreshGlobalHotKey();

#ifdef Q_OS_WIN
    void pollHotKeyState();
    void registerGlobalHotKey();
    void unregisterGlobalHotKey();
#endif

    QKeySequence keySequence;
    QTimer pollTimer;
    bool held = false;
    bool pttEnabled = false;

#ifdef Q_OS_WIN
    bool hotKeyRegistered = false;
    int vk = 0;
#endif
};

} // namespace AV
} // namespace Core
} // namespace Acheron

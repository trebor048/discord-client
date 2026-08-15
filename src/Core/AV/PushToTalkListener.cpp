#include "PushToTalkListener.hpp"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QSettings>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Acheron {
namespace Core {
namespace AV {

namespace {
constexpr const char *kDefaultPttKey = "V";
#ifdef Q_OS_WIN
// Identifier for the system-wide hotkey registration. Must not clash with any
// other hotkey id this process registers (none does today).
constexpr int kGlobalHotKeyId = 0x01;
#endif
}

PushToTalkListener::PushToTalkListener(QObject *parent)
    : QObject(parent)
{
    applyKey(QSettings().value("voice/ptt_key", kDefaultPttKey).toString());

    pollTimer.setInterval(25);
#ifdef Q_OS_WIN
    connect(&pollTimer, &QTimer::timeout, this, &PushToTalkListener::pollHotKeyState);
#endif

    if (auto *app = QCoreApplication::instance()) {
        app->installEventFilter(this);
        app->installNativeEventFilter(this);
    }
}

PushToTalkListener::~PushToTalkListener()
{
#ifdef Q_OS_WIN
    unregisterGlobalHotKey();
#endif
    if (auto *app = QCoreApplication::instance()) {
        // Qt removes event filters on QObject destruction automatically, but
        // the native event filter is installed on the app and must be removed
        // explicitly before this object is torn down.
        app->removeNativeEventFilter(this);
        app->removeEventFilter(this);
    }
}

void PushToTalkListener::setKey(const QString &key)
{
    if (key.isEmpty())
        return;

    applyKey(key);
    // A rebind while the old key is held must release it, otherwise the mic
    // would stay stuck open after the key stops being reported.
    setHeld(false);
}

void PushToTalkListener::setPushToTalkEnabled(bool enabled)
{
    if (pttEnabled == enabled)
        return;

    pttEnabled = enabled;
    // Registering/unregistering happens here so the system-wide hotkey is only
    // captured while push-to-talk is actually active (never swallowing a plain
    // letter key from every other application while PTT is off).
    refreshGlobalHotKey();
}

void PushToTalkListener::applyKey(const QString &key)
{
    QKeySequence sequence(key);
    keySequence = sequence.isEmpty() ? QKeySequence(QString::fromLatin1(kDefaultPttKey)) : sequence;
    refreshGlobalHotKey();
}

void PushToTalkListener::setHeld(bool isHeld)
{
    if (held == isHeld)
        return;

    held = isHeld;
    emit pushToTalkKeyHeld(held);
}

void PushToTalkListener::refreshGlobalHotKey()
{
#ifdef Q_OS_WIN
    unregisterGlobalHotKey();
    pollTimer.stop();
    // A rebind or toggle-off while the old key is held must release it, else
    // the mic would stay open after the key stops being reported.
    setHeld(false);

    if (!pttEnabled || keySequence.isEmpty())
        return;

    const int combined = keySequence[0].toCombined();
    const Qt::Key key = Qt::Key(combined & 0x00FFFFFF);
    const int modifiers = combined & int(Qt::KeyboardModifierMask);

    // Only letters and digits map to a sensible global hotkey. Any other key
    // combo falls back to the in-application event-filter path.
    int keyCode = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        keyCode = 'A' + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        keyCode = '0' + (key - Qt::Key_0);
    else
        return;

    UINT fsModifiers = 0;
    if (modifiers & Qt::CTRL)
        fsModifiers |= MOD_CONTROL;
    if (modifiers & Qt::SHIFT)
        fsModifiers |= MOD_SHIFT;
    if (modifiers & Qt::ALT)
        fsModifiers |= MOD_ALT;
    if (modifiers & Qt::META)
        fsModifiers |= MOD_WIN;
    fsModifiers |= MOD_NOREPEAT;

    // RegisterHotKey gives a single WM_HOTKEY per press (no auto-repeat), so we
    // must poll GetAsyncKeyState on a short timer to catch the release edge.
    if (RegisterHotKey(nullptr, kGlobalHotKeyId, fsModifiers, UINT(keyCode))) {
        vk = keyCode;
        hotKeyRegistered = true;
    }
#endif
}

#ifdef Q_OS_WIN
void PushToTalkListener::unregisterGlobalHotKey()
{
    if (hotKeyRegistered) {
        UnregisterHotKey(nullptr, kGlobalHotKeyId);
        hotKeyRegistered = false;
    }
    vk = 0;
}

void PushToTalkListener::pollHotKeyState()
{
    if (vk == 0) {
        pollTimer.stop();
        return;
    }

    const bool down = (GetAsyncKeyState(UINT(vk)) & 0x8000) != 0;
    setHeld(down);
    if (!down)
        pollTimer.stop();
}
#endif

bool PushToTalkListener::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == kGlobalHotKeyId) {
        // WM_HOTKEY only arrives on the press edge. Reflect it immediately so a
        // very quick tap is not missed, then keep polling until the key lifts.
        pollHotKeyState();
        if (!pollTimer.isActive())
            pollTimer.start();
        if (result)
            *result = 0;
        return true;
    }
#endif
    // QAbstractNativeEventFilter::nativeEventFilter has no callable base
    // implementation (it is pure virtual); unhandled messages return false.
    return false;
}

bool PushToTalkListener::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (!keyEvent->isAutoRepeat() && !keySequence.isEmpty() && keyEvent->key() == keySequence[0]) {
            setHeld(event->type() == QEvent::KeyPress);
        }
    } else if (event->type() == QEvent::ApplicationDeactivate) {
        // Losing focus while the key is held would otherwise leave the mic
        // stuck open since the matching key release never arrives.
        setHeld(false);
    }

    return QObject::eventFilter(watched, event);
}

} // namespace AV
} // namespace Core
} // namespace Acheron

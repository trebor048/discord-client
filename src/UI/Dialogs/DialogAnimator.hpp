#pragma once

#include <QObject>

namespace Acheron {
namespace UI {

/// App-wide enter animation for dialogs that don't animate themselves.
///
/// BasePopup subclasses already fade their content in/out, but the many plain
/// QDialog popups (GuildSettingsDialog, UserProfilePopup, pickers, etc.) appear
/// instantly. This filter fades a top-level QDialog in on QEvent::Show so every
/// popup gets a consistent, config-aware entrance. It skips BasePopup and
/// QMessageBox to avoid double-fading or fighting platform dialogs, and defers
/// to AnimationConfig for reduce-motion / speed.
class DialogAnimator : public QObject
{
    Q_OBJECT
public:
    static DialogAnimator &instance();

    /// Install the filter on the application (idempotent).
    void install();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit DialogAnimator(QObject *parent = nullptr);

    bool installed_ = false;
};

} // namespace UI
} // namespace Acheron

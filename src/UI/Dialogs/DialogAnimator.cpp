#include "DialogAnimator.hpp"

#include "BasePopup.hpp"
#include "Core/AnimationUtils.hpp"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMessageBox>
#include <QWidget>

#include <algorithm>

namespace Acheron {
namespace UI {

DialogAnimator &DialogAnimator::instance()
{
    static DialogAnimator animator;
    return animator;
}

DialogAnimator::DialogAnimator(QObject *parent)
    : QObject(parent)
{
}

void DialogAnimator::install()
{
    if (installed_)
        return;
    installed_ = true;
    qApp->installEventFilter(this);
}

bool DialogAnimator::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Show)
        return QObject::eventFilter(watched, event);

    auto *dlg = qobject_cast<QDialog *>(watched);
    if (!dlg || !dlg->isWindow())
        return QObject::eventFilter(watched, event);

    // BasePopup fades its own content host; skipping avoids a double fade.
    if (qobject_cast<BasePopup *>(dlg))
        return QObject::eventFilter(watched, event);

    // Message boxes are transient platform-style dialogs; leave them alone.
    if (qobject_cast<QMessageBox *>(dlg))
        return QObject::eventFilter(watched, event);

    // An opacity effect composites the whole window offscreen, which breaks
    // native/accelerated surfaces (video) and would double-fade translucent
    // windows or windows that already carry their own graphics effect.
    if (dlg->testAttribute(Qt::WA_TranslucentBackground) || dlg->graphicsEffect())
        return QObject::eventFilter(watched, event);

    const auto children = dlg->findChildren<QWidget *>();
    const bool hasVideo = std::any_of(children.cbegin(), children.cend(), [](const QWidget *w) {
        return qstrcmp(w->metaObject()->className(), "QVideoWidget") == 0;
    });
    if (hasVideo)
        return QObject::eventFilter(watched, event);

    Core::AnimationUtils::fadeIn(dlg, 180);
    return QObject::eventFilter(watched, event);
}

} // namespace UI
} // namespace Acheron

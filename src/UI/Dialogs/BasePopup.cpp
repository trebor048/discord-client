#include "BasePopup.hpp"
#include "Core/AnimationUtils.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include "Core/Theme/Manager.hpp"
#include <QGraphicsDropShadowEffect>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QEvent>

namespace Acheron {
namespace UI {

BasePopup::~BasePopup()
{
    if (parentWidget() && parentWidget()->window())
        parentWidget()->window()->removeEventFilter(this);
}

BasePopup::BasePopup(QWidget *parent) : QDialog(parent, Qt::FramelessWindowHint | Qt::Dialog)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    auto *overlayLayout = new QVBoxLayout(this);
    overlayLayout->setAlignment(Qt::AlignCenter);
    overlayLayout->setContentsMargins(20, 20, 20, 20);

    // Host that carries the fade-in opacity effect. Keeping the shadow on
    // `container` and the opacity effect on `fadeHost` avoids the two effects
    // clobbering each other (a QWidget can only have one graphics effect).
    fadeHost = new QWidget(this);
    fadeHost->setAttribute(Qt::WA_TranslucentBackground, true);
    auto *hostLayout = new QVBoxLayout(fadeHost);
    // Margins give the container's drop shadow room to render inside fadeHost
    // (the opacity effect composites only within fadeHost's bounds).
    hostLayout->setContentsMargins(35, 35, 35, 45);
    hostLayout->setSpacing(0);

    container = new QFrame(fadeHost);
    container->setObjectName("ContentFrame");
    container->setAutoFillBackground(false);
    container->setFrameShape(QFrame::NoFrame);
    container->setStyleSheet(QStringLiteral(
            "#ContentFrame { background: palette(window); border: 1px solid palette(mid); "
            "border-radius: %1px; }")
                                    .arg(Core::Theme::Manager::instance().roundness()));

    // Default for small popups (ConfirmPopup, ThreadBrowserPopup). Subclasses that
    // need more room (e.g. SettingsWindow) override these in their setupUi().
    container->setMinimumWidth(300);
    container->setMaximumWidth(600);
    container->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    auto *shadow = new QGraphicsDropShadowEffect(container);
    shadow->setBlurRadius(25);
    shadow->setXOffset(0);
    shadow->setYOffset(8);
    shadow->setColor(QColor(0, 0, 0, 100));
    container->setGraphicsEffect(shadow);

    hostLayout->addWidget(container);
    overlayLayout->addWidget(fadeHost, 0, Qt::AlignCenter);
}

void BasePopup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, 110));
}

void BasePopup::mousePressEvent(QMouseEvent *event)
{
    if (!fadeHost->geometry().contains(event->pos())) {
        reject();
    } else {
        QDialog::mousePressEvent(event);
    }
}

void BasePopup::accept()
{
    if (exitAnimating) {
        QDialog::accept();
        return;
    }
    exitAnimating = true;
    Core::AnimationUtils::popupExit(fadeHost, [this]() { QDialog::accept(); });
}

void BasePopup::reject()
{
    if (exitAnimating) {
        QDialog::reject();
        return;
    }
    exitAnimating = true;
    Core::AnimationUtils::popupExit(fadeHost, [this]() { QDialog::reject(); });
}

void BasePopup::showEvent(QShowEvent *event)
{
    if (parentWidget() && parentWidget()->window()) {
        QWidget *topLevel = parentWidget()->window();
        setGeometry(topLevel->geometry());
        topLevel->installEventFilter(this);
    }
    QDialog::showEvent(event);
    exitAnimating = false;
    Core::AnimationUtils::popupEnter(fadeHost);
}

void BasePopup::hideEvent(QHideEvent *event)
{
    if (parentWidget() && parentWidget()->window())
        parentWidget()->window()->removeEventFilter(this);
    QDialog::hideEvent(event);
}

bool BasePopup::eventFilter(QObject *obj, QEvent *event)
{
    if (parentWidget() && obj == parentWidget()->window()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
            setGeometry(parentWidget()->window()->geometry());
        }
    }
    return QDialog::eventFilter(obj, event);
}

} // namespace UI
} // namespace Acheron

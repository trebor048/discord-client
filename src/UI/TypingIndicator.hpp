#pragma once

#include <QWidget>
#include <QLabel>
#include <QTimer>
#include <QColor>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <functional>
#include "Core/Snowflake.hpp"
#include "Core/TypingTracker.hpp"

namespace Acheron {
namespace Core {
class ImageManager;
}
namespace UI {

class TypingIndicator : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
public:
    explicit TypingIndicator(QWidget *parent = nullptr);

    using RoleColorResolver = std::function<QColor(Core::Snowflake userId, Core::Snowflake guildId)>;
    void setRoleColorResolver(RoleColorResolver resolver);
    void setImageManager(Core::ImageManager *imgManager);

    void setTypers(const QList<Core::TyperInfo> &typers);

    qreal opacity() const { return opacityEffect ? opacityEffect->opacity() : 1.0; }
    void setOpacity(qreal op);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QLabel *label;
    RoleColorResolver roleColorResolver;
    Core::ImageManager *imageManager = nullptr;

    QString formatText(const QList<Core::TyperInfo> &typers);
    QString coloredName(const Core::TyperInfo &typer);

    QTimer *dotTimer;
    int dotPhase = 0;

    QTimer *dotBounceTimer = nullptr;
    float dotBouncePhase = 0.0f;

    QGraphicsOpacityEffect *opacityEffect = nullptr;
    QPropertyAnimation *fadeAnimation = nullptr;
    bool isAnimatingVisible = false;
};

} // namespace UI
} // namespace Acheron

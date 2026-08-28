#include "TypingIndicator.hpp"
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "Core/AnimationUtils.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include "Core/ImageManager.hpp"

namespace Acheron {
namespace UI {

TypingIndicator::TypingIndicator(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 0, 16, 0);
    layout->setSpacing(6);

    // TyperInfo currently has no avatar hash/url field, so there is no usable
    // thumbnail source here yet. The indicator renders text + animated dots.

    label = new QLabel(this);
    label->setTextFormat(Qt::RichText);
    label->setStyleSheet("font-weight: bold; font-size: 12px;");
    layout->addWidget(label);

    layout->addStretch();

    setFixedHeight(24);
    label->setVisible(false);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    opacityEffect = new QGraphicsOpacityEffect(this);
    opacityEffect->setOpacity(0.0);
    setGraphicsEffect(opacityEffect);

    // Animated dots timer (400ms cycle, 3 phases)
    dotTimer = new QTimer(this);
    dotTimer->setInterval(400);
    connect(dotTimer, &QTimer::timeout, this, [this]() {
        dotPhase = (dotPhase + 1) % 4; // 0=hidden, 1,2,3 = dot count
        update();
    });

    // Smooth bounce animation for the typing dots
    dotBounceTimer = new QTimer(this);
    dotBounceTimer->setInterval(30);
    dotBouncePhase = 0.0f;
    connect(dotBounceTimer, &QTimer::timeout, this, [this]() {
        dotBouncePhase += 0.15f;
        if (dotBouncePhase > 6.2832f)
            dotBouncePhase -= 6.2832f;
        update();
    });

    fadeAnimation = new QPropertyAnimation(opacityEffect, "opacity", this);
    fadeAnimation->setDuration(Core::AnimationConfig::instance().scaled(180));
    fadeAnimation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(fadeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (opacityEffect->opacity() <= 0.0) {
            label->setVisible(false);
            dotTimer->stop();
            isAnimatingVisible = false;
        }
    });
}

void TypingIndicator::setRoleColorResolver(RoleColorResolver resolver)
{
    roleColorResolver = std::move(resolver);
}

void TypingIndicator::setImageManager(Core::ImageManager *imgManager)
{
    imageManager = imgManager;
}

void TypingIndicator::setOpacity(qreal op)
{
    if (opacityEffect)
        opacityEffect->setOpacity(op);
}

void TypingIndicator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPalette pal = palette();

    // Draw animated dots with smooth bounce when visible
    if (label->isVisible() || isAnimatingVisible) {
        int dotYBase = (height() - 4) / 2;
        int dotX = label->isVisible() ? label->geometry().right() + 8 : 16;

        QColor dotColor = pal.color(QPalette::Highlight);
        dotColor.setAlpha(180);

        for (int i = 0; i < 3; ++i) {
            // Smooth bounce using sine wave — each dot has a phase offset
            float offset = std::sin(dotBouncePhase + i * 2.094f) * 0.5f + 0.5f; // 0..1
            int dotRadius = 2 + static_cast<int>(offset * 1.5f);
            int bobOffset = -static_cast<int>(offset * 4.0f);

            QRectF dotRect(dotX + i * 8 - dotRadius,
                           dotYBase + bobOffset - dotRadius,
                           dotRadius * 2, dotRadius * 2);
            p.setBrush(dotColor);
            p.setPen(Qt::NoPen);
            p.drawEllipse(dotRect);
        }
    }
}

void TypingIndicator::setTypers(const QList<Core::TyperInfo> &typers)
{
    if (typers.isEmpty()) {
        if (activityActive_) {
            activityActive_ = false;
            emit activityChanged(false);
        }
        if (!label->isVisible() && opacityEffect->opacity() <= 0.0) {
            dotTimer->stop();
            dotBounceTimer->stop();
            isAnimatingVisible = false;
            return;
        }

        isAnimatingVisible = true;
        label->setVisible(true);
        if (fadeAnimation) {
            fadeAnimation->stop();
            fadeAnimation->setStartValue(opacityEffect->opacity());
            fadeAnimation->setEndValue(0.0);
            fadeAnimation->start();
        }
        dotTimer->stop();
        dotBounceTimer->stop();
        update();
        return;
    }

    if (!activityActive_) {
        activityActive_ = true;
        emit activityChanged(true);
    }

    if (!isAnimatingVisible || opacityEffect->opacity() < 1.0) {
        isAnimatingVisible = true;
        if (fadeAnimation) {
            fadeAnimation->stop();
            fadeAnimation->setStartValue(opacityEffect->opacity());
            fadeAnimation->setEndValue(1.0);
            fadeAnimation->start();
        }
    }

    label->setText(formatText(typers));
    label->setVisible(true);
    dotBounceTimer->start();
    dotTimer->start();
    update();
}

QString TypingIndicator::coloredName(const Core::TyperInfo &typer)
{
    QString escapedName = typer.name.toHtmlEscaped();

    if (roleColorResolver && typer.guildId.has_value()) {
        QColor color = roleColorResolver(typer.userId, typer.guildId.value());
        if (color.isValid())
            return QStringLiteral("<span style=\"color: %1\">%2</span>").arg(color.name(), escapedName);
    }

    return escapedName;
}

QString TypingIndicator::formatText(const QList<Core::TyperInfo> &typers)
{
    if (typers.isEmpty())
        return {};

    const int count = typers.size();

    if (count == 1)
        return coloredName(typers[0]) + QStringLiteral(" is typing");

    if (count == 2)
        return coloredName(typers[0]) + QStringLiteral(" and ") + coloredName(typers[1]) + QStringLiteral(" are typing");

    if (count == 3)
        return coloredName(typers[0]) + QStringLiteral(", ") + coloredName(typers[1]) + QStringLiteral(", and ") + coloredName(typers[2]) + QStringLiteral(" are typing");

    return QStringLiteral("Several people are typing");
}

} // namespace UI
} // namespace Acheron

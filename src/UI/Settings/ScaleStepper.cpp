#include "ScaleStepper.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace Acheron {
namespace UI {

ScaleStepper::ScaleStepper(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    minusButton_ = new QToolButton(this);
    minusButton_->setObjectName(QStringLiteral("scale-minus"));
    minusButton_->setText(QStringLiteral("\u2212"));
    minusButton_->setAutoRepeat(true);
    minusButton_->setFixedSize(24, 24);

    valueLabel_ = new QLabel(this);
    valueLabel_->setObjectName(QStringLiteral("scale-value"));
    valueLabel_->setAlignment(Qt::AlignCenter);
    valueLabel_->setMinimumWidth(44);

    plusButton_ = new QToolButton(this);
    plusButton_->setObjectName(QStringLiteral("scale-plus"));
    plusButton_->setText(QStringLiteral("+"));
    plusButton_->setAutoRepeat(true);
    plusButton_->setFixedSize(24, 24);

    layout->addWidget(minusButton_);
    layout->addWidget(valueLabel_);
    layout->addWidget(plusButton_);

    connect(minusButton_, &QToolButton::clicked, this, [this]() { setValue(value_ - step_); });
    connect(plusButton_, &QToolButton::clicked, this, [this]() { setValue(value_ + step_); });

    refresh();
}

void ScaleStepper::setValue(float value)
{
    const float clamped = qBound(min_, value, max_);
    if (qFuzzyCompare(clamped, value_))
        return;
    value_ = clamped;
    refresh();
    emit valueChanged(value_);
}

void ScaleStepper::setRange(float min, float max)
{
    min_ = qMin(min, max);
    max_ = qMax(min, max);
    setValue(value_);
}

void ScaleStepper::refresh()
{
    valueLabel_->setText(QStringLiteral("%1%").arg(qRound(value_ * 100.0f)));
    minusButton_->setEnabled(value_ > min_ + step_ / 2.0f);
    plusButton_->setEnabled(value_ < max_ - step_ / 2.0f);
}

} // namespace UI
} // namespace Acheron

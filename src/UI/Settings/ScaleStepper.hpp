#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

namespace Acheron {
namespace UI {

/// A compact `[−] <percent> [+]` numeric stepper for the appearance scaling
/// rows. Steps through a [min, max] range; the buttons disable at the bounds.
class ScaleStepper : public QWidget
{
    Q_OBJECT
public:
    explicit ScaleStepper(QWidget *parent = nullptr);

    float value() const { return value_; }
    void setValue(float value);

    void setRange(float min, float max);
    void setStep(float step) { step_ = step; }

signals:
    void valueChanged(float value);

private:
    void refresh();

    QToolButton *minusButton_ = nullptr;
    QToolButton *plusButton_ = nullptr;
    QLabel *valueLabel_ = nullptr;

    float value_ = 1.0f;
    float min_ = 0.80f;
    float max_ = 1.50f;
    float step_ = 0.05f;
};

} // namespace UI
} // namespace Acheron

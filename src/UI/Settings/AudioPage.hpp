#pragma once

#include <QWidget>

class QComboBox;

namespace Acheron {
namespace UI {

class AudioPage : public QWidget
{
    Q_OBJECT
public:
    explicit AudioPage(QWidget *parent = nullptr);

private:
    QComboBox *backendCombo;
};

} // namespace UI
} // namespace Acheron

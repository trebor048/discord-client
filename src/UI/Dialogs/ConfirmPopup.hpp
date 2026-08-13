#pragma once

#include <QDialog>
#include <QFrame>
#include <QEvent>

#include "BasePopup.hpp"

namespace Acheron {
namespace UI {

class ConfirmPopup : public BasePopup
{
    Q_OBJECT
public:
    explicit ConfirmPopup(const QString &title, const QString &text, const QString &yesLabel,
                          QWidget *parent = nullptr);
};

} // namespace UI
} // namespace Acheron

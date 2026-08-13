#pragma once

#include <QDialog>

class QLabel;
class QScrollArea;

namespace Acheron {
namespace UI {

class ShortcutSheet : public QDialog
{
    Q_OBJECT
public:
    explicit ShortcutSheet(QWidget *parent = nullptr);

protected:
    void done(int r) override;
};

} // namespace UI
} // namespace Acheron

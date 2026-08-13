#pragma once

#include <QDialog>
#include <QFrame>

namespace Acheron {
namespace UI {

class BasePopup : public QDialog
{
    Q_OBJECT
public:
    explicit BasePopup(QWidget *parent = nullptr);

protected:
    QFrame *getContainer() const { return container; }

    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    QFrame *container;
    QWidget *fadeHost;
};

} // namespace UI
} // namespace Acheron
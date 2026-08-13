#pragma once

#include <QSplitterHandle>

namespace Acheron {
namespace UI {

class SplitterHandle : public QSplitterHandle
{
    Q_OBJECT
public:
    explicit SplitterHandle(QSplitter *parent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool hovered = false;
};

} // namespace UI
} // namespace Acheron

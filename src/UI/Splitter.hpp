#pragma once

#include <QSplitter>

namespace Acheron {
namespace UI {

class Splitter : public QSplitter
{
    Q_OBJECT
public:
    using QSplitter::QSplitter;

protected:
    QSplitterHandle *createHandle() override;
};

} // namespace UI
} // namespace Acheron

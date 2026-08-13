#include "Splitter.hpp"
#include "SplitterHandle.hpp"

namespace Acheron {
namespace UI {

QSplitterHandle *Splitter::createHandle()
{
    return new SplitterHandle(this);
}

} // namespace UI
} // namespace Acheron

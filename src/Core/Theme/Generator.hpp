#pragma once

#include <QColor>
#include <QHash>
#include <QString>

#include "Core/Theme/Tokens.hpp"

namespace Acheron {
namespace Core {
namespace Theme {

enum class Scheme {
    Monochromatic,
    Analogous,
    Complementary,
    SplitComplementary,
    Triadic,
};

inline constexpr int schemeCount = 5;

QString schemeName(Scheme scheme);
QHash<Token, QColor> generate(const QColor &seed, Scheme scheme, bool dark);

} // namespace Theme
} // namespace Core
} // namespace Acheron

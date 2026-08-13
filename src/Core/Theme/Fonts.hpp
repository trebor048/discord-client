#pragma once

#include <QFont>
#include <QHashFunctions>
#include <QString>

#include <type_traits>
#include <vector>

namespace Acheron {
namespace Core {
namespace Theme {

enum class FontRole {
    Ui,
    Message,
    Code,
};

struct FontDescriptor
{
    FontRole role;
    const char *id;
    const char *label;
    QFont defaultFont;
};

const std::vector<FontDescriptor> &fontRegistry();
const FontDescriptor *findFontById(const QString &id);
const FontDescriptor &fontDescriptor(FontRole role);

inline size_t qHash(FontRole key, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<std::underlying_type_t<FontRole>>(key), seed);
}

} // namespace Theme
} // namespace Core
} // namespace Acheron

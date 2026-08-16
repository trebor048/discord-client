#include "StickerPreferences.hpp"

#include <QSettings>

namespace Acheron {
namespace UI {

namespace {
constexpr int kRecentLimit = 20;
constexpr char kRecentsKey[] = "stickers/recents";
}

QStringList StickerPreferences::loadList(const char *key)
{
    QSettings settings;
    return settings.value(QLatin1String(key)).toStringList();
}

void StickerPreferences::saveList(const char *key, const QStringList &values)
{
    QSettings settings;
    settings.setValue(QLatin1String(key), values);
}

QStringList StickerPreferences::recents()
{
    QStringList values = loadList(kRecentsKey);
    // De-duplicate while preserving most-recent-first order.
    QStringList result;
    for (const QString &value : values) {
        if (result.contains(value))
            continue;
        result.push_back(value);
        if (result.size() >= kRecentLimit)
            break;
    }
    return result;
}

void StickerPreferences::addRecent(const QString &stickerId)
{
    QStringList values = recents();
    values.removeAll(stickerId);
    values.prepend(stickerId);
    if (values.size() > kRecentLimit)
        values = values.mid(0, kRecentLimit);
    saveList(kRecentsKey, values);
}

} // namespace UI
} // namespace Acheron

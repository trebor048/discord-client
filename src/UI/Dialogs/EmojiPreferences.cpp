#include "EmojiPreferences.hpp"

#include "Core/EmojiCatalog.hpp"

#include <QSettings>

namespace Acheron {
namespace UI {

namespace {
constexpr int kRecentLimit = 20;
constexpr int kFavoritesLimit = 100;
constexpr char kRecentsKey[] = "emoji/recents";
constexpr char kFavoritesKey[] = "emoji/favorites";
}

QStringList EmojiPreferences::sanitize(const QStringList &values, int maxCount)
{
    QStringList result;
    result.reserve(values.size());
    for (const QString &value : values) {
        if (!Core::EmojiCatalog::isSupportedSelection(value))
            continue;
        if (result.contains(value))
            continue;
        result.push_back(value);
        if (result.size() >= maxCount)
            break;
    }
    return result;
}

QStringList EmojiPreferences::loadList(const char *key)
{
    QSettings settings;
    return settings.value(QLatin1String(key)).toStringList();
}

void EmojiPreferences::saveList(const char *key, const QStringList &values)
{
    QSettings settings;
    settings.setValue(QLatin1String(key), values);
}

QStringList EmojiPreferences::recents()
{
    const QStringList sanitized = sanitize(loadList(kRecentsKey), kRecentLimit);
    saveList(kRecentsKey, sanitized);
    return sanitized;
}

QStringList EmojiPreferences::favorites()
{
    const QStringList sanitized = sanitize(loadList(kFavoritesKey), kFavoritesLimit);
    saveList(kFavoritesKey, sanitized);
    return sanitized;
}

void EmojiPreferences::setRecents(const QStringList &values)
{
    saveList(kRecentsKey, sanitize(values, kRecentLimit));
}

void EmojiPreferences::setFavorites(const QStringList &values)
{
    saveList(kFavoritesKey, sanitize(values, kFavoritesLimit));
}

void EmojiPreferences::addRecent(const QString &value)
{
    if (!Core::EmojiCatalog::isSupportedSelection(value))
        return;

    QStringList values = recents();
    values.removeAll(value);
    values.prepend(value);
    setRecents(values);
}

bool EmojiPreferences::isFavorite(const QString &value)
{
    return favorites().contains(value);
}

void EmojiPreferences::setFavorite(const QString &value, bool favorite)
{
    if (!Core::EmojiCatalog::isSupportedSelection(value))
        return;

    QStringList values = favorites();
    values.removeAll(value);
    if (favorite)
        values.prepend(value);
    setFavorites(values);
}

} // namespace UI
} // namespace Acheron

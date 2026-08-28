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

// In-memory mirrors of the persisted lists. recents()/favorites() previously
// read AND wrote QSettings on every call (hit per emoji click, grid rebuild,
// and favorite toggle). The QSettings round-trip now happens once per list,
// with write-through only when the sanitized value actually changed, so the
// on-disk format and contents stay identical.
struct CachedLists
{
    QStringList recents;
    QStringList favorites;
    bool recentsLoaded = false;
    bool favoritesLoaded = false;
};

CachedLists &cachedLists()
{
    static CachedLists cache;
    return cache;
}
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

bool EmojiPreferences::refreshMirror(const char *key, QStringList &mirror, int limit)
{
    const QStringList sanitized = sanitize(mirror, limit);
    if (sanitized == mirror)
        return false;
    mirror = sanitized;
    saveList(key, sanitized);
    return true;
}

QStringList EmojiPreferences::recents()
{
    CachedLists &cache = cachedLists();
    if (!cache.recentsLoaded) {
        const QStringList raw = loadList(kRecentsKey);
        const QStringList sanitized = sanitize(raw, kRecentLimit);
        cache.recents = sanitized;
        cache.recentsLoaded = true;
        // Preserve the historical write-on-read self-heal, but only once and
        // only when sanitization actually changed the stored value.
        if (sanitized != raw)
            saveList(kRecentsKey, sanitized);
    } else {
        // Emoji support can change between reads (a guild emoji is removed),
        // so re-filter the mirror without a QSettings round-trip.
        refreshMirror(kRecentsKey, cache.recents, kRecentLimit);
    }
    return cache.recents;
}

QStringList EmojiPreferences::favorites()
{
    CachedLists &cache = cachedLists();
    if (!cache.favoritesLoaded) {
        const QStringList raw = loadList(kFavoritesKey);
        const QStringList sanitized = sanitize(raw, kFavoritesLimit);
        cache.favorites = sanitized;
        cache.favoritesLoaded = true;
        if (sanitized != raw)
            saveList(kFavoritesKey, sanitized);
    } else {
        refreshMirror(kFavoritesKey, cache.favorites, kFavoritesLimit);
    }
    return cache.favorites;
}

void EmojiPreferences::setRecents(const QStringList &values)
{
    const QStringList sanitized = sanitize(values, kRecentLimit);
    CachedLists &cache = cachedLists();
    if (!cache.recentsLoaded || cache.recents != sanitized) {
        cache.recents = sanitized;
        cache.recentsLoaded = true;
        saveList(kRecentsKey, sanitized);
    }
}

void EmojiPreferences::setFavorites(const QStringList &values)
{
    const QStringList sanitized = sanitize(values, kFavoritesLimit);
    CachedLists &cache = cachedLists();
    if (!cache.favoritesLoaded || cache.favorites != sanitized) {
        cache.favorites = sanitized;
        cache.favoritesLoaded = true;
        saveList(kFavoritesKey, sanitized);
    }
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

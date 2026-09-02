#include "EmojiCatalog.hpp"

#include <QMutex>
#include <QRegularExpression>

#include <algorithm>
#include <new>

namespace Acheron {
namespace Core {

namespace {

const QVector<EmojiCatalogItem> &unicodeEmojiData()
{
    static const QVector<EmojiCatalogItem> items{
#include "EmojiCatalogData.inc"
    };
    return items;
}

// Hash index over unicodeEmojiData() so per-emoji lookups are O(1) instead of
// scanning the whole (~4k entry) table for every recents/favorites entry.
const QHash<QString, int> &unicodeIndexByEmoji()
{
    static const QHash<QString, int> index = [] {
        QHash<QString, int> map;
        const auto &data = unicodeEmojiData();
        map.reserve(data.size());
        for (int i = 0; i < data.size(); ++i)
            map.insert(data[i].unicodeEmoji, i);
        return map;
    }();
    return index;
}

QString normalizeQuery(const QString &text)
{
    return text.trimmed().toCaseFolded();
}

// Precomputed folded-name index over combinedEmojiData(). Folding ~4k names on
// every keystroke/lookup (search, completions, exact name lookups, category
// scans) is measurable; the index is rebuilt in the same locked pass as the
// combined data and invalidated by the same dirty flag.
struct EmojiSearchIndex
{
    // folded name -> index of the FIRST matching item (scan order), preserving
    // the original "first match wins" semantics of the linear scans.
    QHash<QString, int> exactAny;      // any item
    QHash<QString, int> exactUnicode;  // non-custom items only
    // Parallel to combinedEmojiData(): pre-folded names/categories.
    QVector<QString> foldedNames;
    QVector<QString> foldedCategories;
};

EmojiSearchIndex &emojiSearchIndex()
{
    static EmojiSearchIndex index;
    return index;
}

} // namespace

static QMutex &customEmojiMutex()
{
    static QMutex mutex;
    return mutex;
}

static QHash<QString, EmojiCatalogItem> &customEmojiRegistry()
{
    static QHash<QString, EmojiCatalogItem> registry;
    return registry;
}

static quint64 &customEmojiGenerationCounter()
{
    static quint64 counter = 0;
    return counter;
}

quint64 EmojiCatalog::customEmojiGeneration()
{
    return customEmojiGenerationCounter();
}

static bool &combinedEmojiDirty()
{
    static bool dirty = true;
    return dirty;
}

static const QVector<EmojiCatalogItem> &combinedEmojiData()
{
    static QVector<EmojiCatalogItem> combined;
    const QMutexLocker locker(&customEmojiMutex());
    if (combinedEmojiDirty()) {
        try {
            // Build the new catalog and its search index off to the side, then
            // commit both only after the whole rebuild succeeded, so callers
            // never observe a partially built catalog or an index that is out
            // of sync with the data it indexes.
            QVector<EmojiCatalogItem> fresh = unicodeEmojiData();
            fresh.reserve(fresh.size() + customEmojiRegistry().size());
            for (const auto &item : customEmojiRegistry())
                fresh.push_back(item);

            // Rebuild the folded-name index in the same locked pass so it
            // always matches the combined data exactly.
            EmojiSearchIndex freshIndex;
            freshIndex.foldedNames.resize(fresh.size());
            freshIndex.foldedCategories.resize(fresh.size());
            for (int i = 0; i < fresh.size(); ++i) {
                const EmojiCatalogItem &item = fresh[i];
                const QString foldedName = item.name.toCaseFolded();
                freshIndex.foldedNames[i] = foldedName;
                freshIndex.foldedCategories[i] = item.category.toCaseFolded();
                if (!freshIndex.exactAny.contains(foldedName))
                    freshIndex.exactAny.insert(foldedName, i);
                if (!item.isCustom() && !freshIndex.exactUnicode.contains(foldedName))
                    freshIndex.exactUnicode.insert(foldedName, i);
            }

            combined = std::move(fresh);
            emojiSearchIndex() = std::move(freshIndex);
            combinedEmojiDirty() = false;
        } catch (const std::bad_alloc &) {
            // Out of memory while rebuilding: leave the previous (still valid)
            // data in place and keep the dirty flag set so a later call
            // retries. Degrade gracefully instead of letting the allocation
            // failure escape into Qt's event dispatch, which would terminate
            // the whole process.
        }
    }
    return combined;
}

const QVector<EmojiCatalogItem> &EmojiCatalog::items()
{
    return combinedEmojiData();
}

QStringList EmojiCatalog::categoryNames()
{
    return {
        QStringLiteral("smileys"),
        QStringLiteral("people"),
        QStringLiteral("animals"),
        QStringLiteral("food"),
        QStringLiteral("travel"),
        QStringLiteral("activities"),
        QStringLiteral("objects"),
        QStringLiteral("symbols"),
        QStringLiteral("flags"),
    };
}

QVector<EmojiCatalogItem> EmojiCatalog::itemsForCategory(const QString &category)
{
    QVector<EmojiCatalogItem> results;
    const QString needle = category.toCaseFolded();
    const auto &data = items();
    const auto &index = emojiSearchIndex();
    for (int i = 0; i < data.size(); ++i) {
        if (index.foldedCategories[i] == needle)
            results.push_back(data[i]);
    }
    return results;
}

QVector<EmojiCatalogItem> EmojiCatalog::search(const QString &query)
{
    const QString needle = normalizeQuery(query);
    QVector<EmojiCatalogItem> results;
    if (needle.isEmpty())
        return items();

    const auto &data = items();
    const auto &index = emojiSearchIndex();
    for (int i = 0; i < data.size(); ++i) {
        if (index.foldedNames[i].contains(needle))
            results.push_back(data[i]);
    }

    std::sort(results.begin(), results.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    return results;
}

QStringList EmojiCatalog::completionNames(const QString &prefix)
{
    QStringList names;
    const QString needle = normalizeQuery(prefix);
    const auto &data = items();
    const auto &index = emojiSearchIndex();
    for (int i = 0; i < data.size(); ++i) {
        if (needle.isEmpty() || index.foldedNames[i].startsWith(needle))
            names.append(data[i].name);
    }
    return names;
}

QString EmojiCatalog::unicodeForName(const QString &name)
{
    const QString needle = normalizeQuery(name);
    const auto &data = items();
    const auto &index = emojiSearchIndex();
    const auto it = index.exactUnicode.constFind(needle);
    if (it == index.exactUnicode.constEnd())
        return {};
    return data[it.value()].unicodeEmoji;
}

QString EmojiCatalog::valueForName(const QString &name)
{
    const QString needle = normalizeQuery(name);
    const auto &data = items();
    const auto &index = emojiSearchIndex();
    const auto it = index.exactAny.constFind(needle);
    if (it == index.exactAny.constEnd())
        return {};
    return data[it.value()].selectionValue();
}

std::optional<EmojiSelectionValue> EmojiCatalog::parseCustomEmoji(const QString &value)
{
    static const QRegularExpression customRe(QStringLiteral(
            R"(^<(a?):([A-Za-z0-9_]{1,32}):(\d{17,20})>$)"));
    const auto match = customRe.match(value);
    if (!match.hasMatch())
        return std::nullopt;

    EmojiSelectionValue selection;
    selection.raw = value;
    selection.isCustom = true;
    selection.animated = match.captured(1) == QStringLiteral("a");
    selection.name = match.captured(2);
    selection.customId = match.captured(3);
    return selection;
}

QString EmojiCatalog::cdnUrlForSelection(const QString &value, int size)
{
    const auto parsed = parseCustomEmoji(value);
    if (!parsed)
        return {};
    return QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=%3&quality=lossless")
            .arg(parsed->customId, parsed->animated ? QStringLiteral("gif") : QStringLiteral("png"))
            .arg(size);
}

bool EmojiCatalog::isSupportedSelection(const QString &value)
{
    if (value.isEmpty())
        return false;

    if (const auto parsed = parseCustomEmoji(value)) {
        const QMutexLocker locker(&customEmojiMutex());
        return customEmojiRegistry().contains(parsed->customId);
    }

    return unicodeIndexByEmoji().contains(value);
}

EmojiSelectionValue EmojiCatalog::selectionForUnicode(const QString &unicodeEmoji)
{
    return selectionForRaw(unicodeEmoji).value_or(EmojiSelectionValue{});
}

std::optional<EmojiSelectionValue> EmojiCatalog::selectionForRaw(const QString &value)
{
    if (value.isEmpty())
        return std::nullopt;

    if (const auto parsed = parseCustomEmoji(value)) {
        const QMutexLocker locker(&customEmojiMutex());
        if (customEmojiRegistry().contains(parsed->customId))
            return parsed;
        return std::nullopt;
    }

    if (unicodeIndexByEmoji().contains(value)) {
        EmojiSelectionValue selection;
        selection.raw = value;
        return selection;
    }
    return std::nullopt;
}

const EmojiCatalogItem *EmojiCatalog::itemForUnicode(const QString &unicodeEmoji)
{
    const auto it = unicodeIndexByEmoji().constFind(unicodeEmoji);
    if (it == unicodeIndexByEmoji().constEnd())
        return nullptr;
    return &unicodeEmojiData()[it.value()];
}

std::optional<EmojiCatalogItem> EmojiCatalog::itemForCustomId(const QString &customId)
{
    const QMutexLocker locker(&customEmojiMutex());
    const auto it = customEmojiRegistry().constFind(customId);
    if (it == customEmojiRegistry().constEnd())
        return std::nullopt;
    return it.value();
}

void EmojiCatalog::registerCustomEmoji(const EmojiCatalogItem &item)
{
    if (!item.isCustom() || item.customId.isEmpty() || item.name.isEmpty())
        return;

    const QMutexLocker locker(&customEmojiMutex());
    customEmojiRegistry().insert(item.customId, item);
    combinedEmojiDirty() = true;
    ++customEmojiGenerationCounter();
}

void EmojiCatalog::registerCustomEmojis(const QVector<EmojiCatalogItem> &items)
{
    const QMutexLocker locker(&customEmojiMutex());
    for (const auto &item : items) {
        if (!item.isCustom() || item.customId.isEmpty() || item.name.isEmpty())
            continue;
        customEmojiRegistry().insert(item.customId, item);
    }
    combinedEmojiDirty() = true;
    ++customEmojiGenerationCounter();
}

void EmojiCatalog::unregisterCustomEmoji(const QString &customId)
{
    const QMutexLocker locker(&customEmojiMutex());
    customEmojiRegistry().remove(customId);
    combinedEmojiDirty() = true;
    ++customEmojiGenerationCounter();
}

void EmojiCatalog::unregisterCustomEmojisByGuild(const QString &guildId)
{
    if (guildId.isEmpty())
        return;

    const QMutexLocker locker(&customEmojiMutex());
    auto &registry = customEmojiRegistry();
    QList<QString> toRemove;
    for (auto it = registry.constBegin(); it != registry.constEnd(); ++it) {
        if (it.value().guildId == guildId)
            toRemove.append(it.key());
    }
    for (const auto &key : toRemove)
        registry.remove(key);
    combinedEmojiDirty() = true;
    ++customEmojiGenerationCounter();
}

void EmojiCatalog::clearCustomEmojis()
{
    const QMutexLocker locker(&customEmojiMutex());
    customEmojiRegistry().clear();
    combinedEmojiDirty() = true;
    ++customEmojiGenerationCounter();
}

QVector<EmojiCatalogItem> EmojiCatalog::customEmojis()
{
    const QMutexLocker locker(&customEmojiMutex());
    try {
        return customEmojiRegistry().values().toVector();
    } catch (const std::bad_alloc &) {
        // Copying the registry (a QHash<QString, EmojiCatalogItem>) into a
        // QVector allocates a contiguous buffer that can fail under memory
        // pressure — and the emoji picker's Server tab does this during dialog
        // construction. Return an empty list so the picker degrades to an
        // empty grid instead of letting the exception escape into Qt's event
        // dispatch, which terminates the whole process.
        return {};
    }
}

std::optional<EmojiSelectionValue> EmojiSelectionValue::fromRaw(const QString &raw)
{
    return EmojiCatalog::selectionForRaw(raw);
}

} // namespace Core
} // namespace Acheron

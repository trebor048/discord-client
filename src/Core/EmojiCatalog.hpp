#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace Acheron {
namespace Core {

struct EmojiCatalogItem
{
    QString name;
    QString unicodeEmoji;
    QString category;
    QString customId; // empty for Unicode emojis, Discord emoji snowflake otherwise
    bool animated = false;
    QString guildName; // only meaningful for custom emojis
    QString guildId;   // Discord snowflake of the owning guild

    [[nodiscard]] bool isCustom() const { return !customId.isEmpty(); }

    [[nodiscard]] QString selectionValue() const
    {
        if (isCustom())
            return QStringLiteral("<%1:%2:%3>").arg(animated ? QStringLiteral("a") : QString(),
                                                     name, customId);
        return unicodeEmoji;
    }

    [[nodiscard]] QString cdnUrl(int size = 48) const
    {
        if (!isCustom())
            return {};
        return QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=%3&quality=lossless")
                .arg(customId, animated ? QStringLiteral("gif") : QStringLiteral("png"))
                .arg(size);
    }

    [[nodiscard]] friend bool operator==(const EmojiCatalogItem &a, const EmojiCatalogItem &b)
    {
        return a.name == b.name && a.unicodeEmoji == b.unicodeEmoji && a.category == b.category
               && a.customId == b.customId && a.animated == b.animated
               && a.guildName == b.guildName && a.guildId == b.guildId;
    }

    [[nodiscard]] friend bool operator!=(const EmojiCatalogItem &a, const EmojiCatalogItem &b)
    {
        return !(a == b);
    }
};

struct EmojiSelectionValue
{
    QString raw;
    bool isCustom = false;
    QString customId;
    QString name;
    bool animated = false;

    [[nodiscard]] bool isValid() const { return !raw.isEmpty(); }

    static std::optional<EmojiSelectionValue> fromRaw(const QString &raw);
};

class EmojiCatalog
{
public:
    static const QVector<EmojiCatalogItem> &items();
    static QVector<EmojiCatalogItem> search(const QString &query);
    static QStringList completionNames(const QString &prefix = {});
    static QString unicodeForName(const QString &name);
    static QString valueForName(const QString &name);
    static bool isSupportedSelection(const QString &value);
    static EmojiSelectionValue selectionForUnicode(const QString &unicodeEmoji);

    static QStringList categoryNames();
    static QVector<EmojiCatalogItem> itemsForCategory(const QString &category);

    static void registerCustomEmoji(const EmojiCatalogItem &item);
    static void registerCustomEmojis(const QVector<EmojiCatalogItem> &items);
    static void unregisterCustomEmoji(const QString &customId);
    static void unregisterCustomEmojisByGuild(const QString &guildId);

    // Monotonic counter bumped on every custom-emoji registry mutation.
    // Consumers that cache derived views of items() (e.g. autocomplete search
    // indexes) can compare it to detect content changes that leave the item
    // COUNT unchanged (switching guilds with the same number of custom emoji).
    static quint64 customEmojiGeneration();
    static void clearCustomEmojis();
    static QVector<EmojiCatalogItem> customEmojis();

    static std::optional<EmojiSelectionValue> selectionForRaw(const QString &value);

    // Returns the CDN url for a custom-emoji selection token (`<:name:id>` or
    // `<a:name:id>`), or an empty string when `value` is not a custom token.
    // Mirrors EmojiCatalogItem::cdnUrl without needing the item in the catalog
    // (the token itself carries the id + animated flag).
    static QString cdnUrlForSelection(const QString &value, int size = 48);

    // O(1) indexed lookups; return nullptr when the value is not in the catalog.
    static const EmojiCatalogItem *itemForUnicode(const QString &unicodeEmoji);
    static std::optional<EmojiCatalogItem> itemForCustomId(const QString &customId);

private:
    static std::optional<EmojiSelectionValue> parseCustomEmoji(const QString &value);
};

} // namespace Core
} // namespace Acheron

#pragma once

#include <QStringList>

namespace Acheron {
namespace UI {

// Persists recently-used sticker IDs (mirrors EmojiPreferences, but keyed by
// sticker snowflake string since stickers have no static catalog).
class StickerPreferences
{
public:
    static QStringList recents();
    static void addRecent(const QString &stickerId);

private:
    static QStringList loadList(const char *key);
    static void saveList(const char *key, const QStringList &values);
};

} // namespace UI
} // namespace Acheron

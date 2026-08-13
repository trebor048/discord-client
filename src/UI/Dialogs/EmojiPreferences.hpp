#pragma once

#include <QStringList>

namespace Acheron {
namespace UI {

class EmojiPreferences
{
public:
    static QStringList recents();
    static QStringList favorites();
    static void setRecents(const QStringList &values);
    static void setFavorites(const QStringList &values);
    static void addRecent(const QString &value);
    static bool isFavorite(const QString &value);
    static void setFavorite(const QString &value, bool favorite);

private:
    static QStringList loadList(const char *key);
    static void saveList(const char *key, const QStringList &values);
    static QStringList sanitize(const QStringList &values, int maxCount);
};

} // namespace UI
} // namespace Acheron

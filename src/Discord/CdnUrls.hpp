#pragma once

#include <QString>
#include <QUrl>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace Discord {
enum class StickerFormatType : int;
namespace Cdn {

inline QString assetExtension(const QString &hash)
{
    return hash.startsWith(QStringLiteral("a_")) ? QStringLiteral("gif") : QStringLiteral("png");
}

inline QUrl userAvatar(Core::Snowflake userId, const QString &hash, int size = 128)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/avatars/%1/%2.%3?size=%4")
                        .arg(QString::number(quint64(userId)), hash, assetExtension(hash),
                             QString::number(size)));
}

inline QUrl guildMemberAvatar(Core::Snowflake guildId, Core::Snowflake userId,
                              const QString &hash, int size = 128)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral(
                        "https://cdn.discordapp.com/guilds/%1/users/%2/avatars/%3.%4?size=%5")
                        .arg(QString::number(quint64(guildId)),
                             QString::number(quint64(userId)), hash, assetExtension(hash),
                             QString::number(size)));
}

inline QUrl userBanner(Core::Snowflake userId, const QString &hash, int size = 600)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/banners/%1/%2.%3?size=%4")
                        .arg(QString::number(quint64(userId)), hash, assetExtension(hash),
                             QString::number(size)));
}

inline QUrl guildIcon(Core::Snowflake guildId, const QString &hash, int size = 64)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/icons/%1/%2.%3?size=%4")
                        .arg(QString::number(quint64(guildId)), hash, assetExtension(hash),
                             QString::number(size)));
}

inline QUrl channelIcon(Core::Snowflake channelId, const QString &hash, int size = 64)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/channel-icons/%1/%2.png?size=%3")
                        .arg(QString::number(quint64(channelId)), hash, QString::number(size)));
}

inline QUrl applicationIcon(Core::Snowflake appId, const QString &hash, int size = 64)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/app-icons/%1/%2.%3?size=%4")
                        .arg(QString::number(quint64(appId)), hash, assetExtension(hash),
                             QString::number(size)));
}

inline QUrl badgeIcon(const QString &hash, int size = 64)
{
    if (hash.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/badge-icons/%1.png?size=%2")
                        .arg(hash, QString::number(size)));
}

inline QUrl emojiImage(const QString &emojiId, bool animated, int size = 128)
{
    if (emojiId.isEmpty())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/emojis/%1.%2?size=%3")
                        .arg(emojiId,
                             animated ? QStringLiteral("gif") : QStringLiteral("png"),
                             QString::number(size)));
}

inline QUrl emoji(Core::Snowflake emojiId, int size = 48)
{
    if (!emojiId.isValid())
        return {};
    return QUrl(QStringLiteral("https://cdn.discordapp.com/emojis/%1.webp?size=%2")
                        .arg(QString::number(quint64(emojiId)), QString::number(size)));
}

QUrl connectionIcon(const QString &type);
QUrl stickerImage(Core::Snowflake stickerId, StickerFormatType format, int size = 160);

} // namespace Cdn
} // namespace Discord
} // namespace Acheron

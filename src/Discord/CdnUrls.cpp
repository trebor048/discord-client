#include "CdnUrls.hpp"
#include "Entities.hpp"
#include "Core/Logging.hpp"

#include <QHash>

namespace Acheron {
namespace Discord {
namespace Cdn {

QUrl connectionIcon(const QString &type)
{
    static const QHash<QString, QString> table = {
        { QStringLiteral("twitch"), QStringLiteral("45900de231038a6d") },
        { QStringLiteral("youtube"), QStringLiteral("c569f6e10f1fa390") },
        { QStringLiteral("spotify"), QStringLiteral("40518c6ced9fed93") },
        { QStringLiteral("github"), QStringLiteral("732989e3c998e808") },
        { QStringLiteral("twitter"), QStringLiteral("afaf5671c31d4941") },
        { QStringLiteral("reddit"), QStringLiteral("afc3f4fa75178004") },
        { QStringLiteral("steam"), QStringLiteral("07ca603d9273b502") },
        { QStringLiteral("xbox"), QStringLiteral("09781ebf5509f48e") },
        { QStringLiteral("playstation"), QStringLiteral("46a23515a87620b9") },
        { QStringLiteral("facebook"), QStringLiteral("bda550439e2a1af0") },
        { QStringLiteral("battlenet"), QStringLiteral("0e1a419c00a471a2") },
        { QStringLiteral("bluesky"), QStringLiteral("8a42ef6bc4dd4e0d") },
        { QStringLiteral("bungie"), QStringLiteral("a35b652323167799") },
        { QStringLiteral("leagueoflegends"), QStringLiteral("fc30b78cd5e1db04") },
        { QStringLiteral("epicgames"), QStringLiteral("ecf035f3cf08a860") },
        { QStringLiteral("riotgames"), QStringLiteral("fa9e14fc0fd3680f") },
        { QStringLiteral("roblox"), QStringLiteral("5d078bf414a0124c") },
        { QStringLiteral("tiktok"), QStringLiteral("d585ec74cbd4c723") },
        { QStringLiteral("instagram"), QStringLiteral("ea0692691904fca9") },
        { QStringLiteral("paypal"), QStringLiteral("81a39a4672d62d60") },
        { QStringLiteral("ebay"), QStringLiteral("d0f7fa72a2ca6388") },
        { QStringLiteral("mastodon"), QStringLiteral("53fcd6d102d44c2b") },
        { QStringLiteral("crunchyroll"), QStringLiteral("4e56bb1ecb9b9dc3") },
        { QStringLiteral("skype"), QStringLiteral("4f2cb7753e5d5e61") },
        { QStringLiteral("samsung"), QStringLiteral("d50e7459b395e5d0") },
        { QStringLiteral("domain"), QStringLiteral("fe9b03f9b097e2a9") },
        { QStringLiteral("amazon-music"), QStringLiteral("cda6fb74e92e32f1") },
    };
    auto it = table.constFind(type);
    if (it == table.constEnd())
        return {};
    return QUrl(QStringLiteral("https://discord.com/assets/%1.png").arg(it.value()));
}

QUrl stickerImage(Core::Snowflake stickerId, StickerFormatType format, int size)
{
    QString ext;
    switch (format) {
    case StickerFormatType::PNG:
    case StickerFormatType::APNG:
        ext = QStringLiteral("png");
        break;
    case StickerFormatType::GIF:
        ext = QStringLiteral("gif");
        break;
    case StickerFormatType::Lottie:
        return {};
    default:
        qCWarning(LogDiscord) << "Unknown sticker format:" << static_cast<int>(format);
        return {};
    }
    return QUrl(QStringLiteral("https://cdn.discordapp.com/stickers/%1.%2?size=%3")
                        .arg(QString::number(quint64(stickerId)), ext, QString::number(size)));
}

} // namespace Cdn
} // namespace Discord
} // namespace Acheron

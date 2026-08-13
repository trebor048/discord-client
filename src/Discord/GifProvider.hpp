#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QList>
#include <QSet>
#include <QSize>
#include <QNetworkAccessManager>
#include <functional>

class QNetworkReply;
class QJsonDocument;
class QJsonObject;
class QJsonArray;

namespace Acheron {
namespace Discord {

struct GifMediaFormat
{
    QUrl url;
    QSize dimensions;
    qint64 size = 0; // bytes, when known
};

struct GifItem
{
    QString id;
    QString title;
    QString contentDescription;
    QString url;          // canonical share URL

    GifMediaFormat preview;   // small GIF for thumbnail display (~fixed_width)
    GifMediaFormat full;      // full-size GIF for sending/saving (~original)
    GifMediaFormat tinygif;   // tiny preview for grid (~downsized)
    GifMediaFormat mp4;       // mp4 if available
    bool hasAudio = false;
};

struct GifCategory
{
    QString searchTerm;  // e.g. "reactions"
    QString name;        // display name
    QUrl thumbnailUrl;
};

using GifSearchCallback = std::function<void(const QList<GifItem> &, bool hasMore)>;
using GifCategoriesCallback = std::function<void(const QList<GifCategory> &)>;

/**
 * Provides access to the GIF picker backends.
 *
 * Supported providers:
 *   - Tenor: API key stored in QSettings under "tenor/apiKey"
 *   - GIPHY: API key stored in QSettings under "giphy/apiKey"
 *   - Klipy: API key stored in QSettings under "klipy/apiKey"
 */
class GifProvider : public QObject
{
    Q_OBJECT
public:
    enum class Provider {
        Tenor,
        Giphy,
        Klipy,
    };

    explicit GifProvider(QObject *parent = nullptr);
    ~GifProvider() override;

    [[nodiscard]] Provider provider() const { return m_provider; }
    void setProvider(Provider provider);

    /// Search for GIFs matching @p query. @p offset is the pagination offset.
    void search(const QString &query, int offset, GifSearchCallback callback);

    /// Fetch trending / featured GIFs. @p offset is the pagination offset.
    void trending(int offset, GifSearchCallback callback);

    /// Fetch available categories (search suggestions).
    void categories(GifCategoriesCallback callback);

    /// Fetch auto-complete suggestions for a partial query.
    void suggestions(const QString &partial, std::function<void(const QStringList &)> callback);

    /// Record that a GIF was selected. Some providers use this for share tracking.
    void recordSelection(const GifItem &item, const QString &query);

    /// Cancel all in-flight requests.
    void cancelAll();

    /// Returns the next pagination offset from the last completed request.
    [[nodiscard]] int nextOffset() const { return m_nextOffset; }

    static constexpr int kDefaultLimit = 20;

    /// Returns the configured GIPHY API key, or empty if not set.
    static QString apiKey();
    [[nodiscard]] static QString apiKey(Provider provider);
    [[nodiscard]] static Provider providerFromSettings(const QString &value);
    [[nodiscard]] static QString providerToSettings(Provider provider);
    [[nodiscard]] static QString providerDisplayName(Provider provider);
    [[nodiscard]] static bool providerNeedsKey(Provider provider);
    [[nodiscard]] static Provider defaultProvider();

private:
    void executeRequest(const QUrl &url, std::function<void(const QJsonDocument &)> callback);
    static QList<GifItem> parseResults(const QJsonArray &results);
    static QList<GifCategory> parseCategories(const QJsonArray &categories);
    static GifMediaFormat parseMediaFormat(const QJsonObject &images, const QStringList &keys);

    QNetworkAccessManager *m_nam;
    QSet<QNetworkReply *> m_pending;
    Provider m_provider;
    QString m_nextPageToken;
    int m_nextOffset = 0;
    int m_lastCount = 0;
    int m_lastTotal = 0;

    static constexpr const char *kGiphyBase = "https://api.giphy.com/v1/gifs";
    static constexpr const char *kGiphySettingsKey = "giphy/apiKey";
    static constexpr const char *kProviderSettingsKey = "gif/provider";
    static constexpr const char *kKlipySettingsKey = "klipy/apiKey";
    static constexpr const char *kTenorBase = "https://tenor.googleapis.com/v2";
    static constexpr const char *kTenorApiKeySettingsKey = "tenor/apiKey";
    static constexpr const char *kTenorClientKey = "tenor_web";
    static constexpr int kMaxPreviewSize = 200; // max width for grid thumbnails
};

} // namespace Discord
} // namespace Acheron

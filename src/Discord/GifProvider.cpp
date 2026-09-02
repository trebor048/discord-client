#include "GifProvider.hpp"
#include "Core/Logging.hpp"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QSettings>

namespace Acheron {
namespace Discord {

namespace {
constexpr auto kProviderSettingsKey = "gif/provider";
constexpr auto kGiphySettingsKey = "giphy/apiKey";
constexpr auto kKlipySettingsKey = "klipy/apiKey";
constexpr auto kTenorApiKeySettingsKey = "tenor/apiKey";
constexpr auto kTenorBase = "https://tenor.googleapis.com/v2";
constexpr auto kTenorClientKey = "tenor_web";

GifProvider::Provider defaultProviderFromSettings()
{
    QSettings settings;
    const QString stored = settings.value(QString::fromLatin1(kProviderSettingsKey)).toString();
    if (!stored.isEmpty())
        return GifProvider::providerFromSettings(stored);

    if (!settings.value(QString::fromLatin1(kGiphySettingsKey)).toString().isEmpty())
        return GifProvider::Provider::Giphy;

    return GifProvider::Provider::Tenor;
}

GifMediaFormat parseTenorMediaFormat(const QJsonObject &formats, const QStringList &keys)
{
    GifMediaFormat result;
    for (const auto &key : keys) {
        const auto fmt = formats.value(key).toObject();
        if (fmt.isEmpty())
            continue;

        result.url = QUrl(fmt.value(QStringLiteral("url")).toString());
        if (!result.url.isValid())
            continue;

        const auto dims = fmt.value(QStringLiteral("dims")).toArray();
        const int w = dims.size() > 0 ? dims.at(0).toInt() : fmt.value(QStringLiteral("width")).toInt();
        const int h = dims.size() > 1 ? dims.at(1).toInt() : fmt.value(QStringLiteral("height")).toInt();
        if (w > 0 && h > 0)
            result.dimensions = QSize(w, h);
        result.size = fmt.value(QStringLiteral("size")).toVariant().toLongLong();
        break;
    }
    return result;
}

QList<GifItem> parseTenorResults(const QJsonArray &results)
{
    QList<GifItem> items;
    items.reserve(results.size());

    for (const auto &val : results) {
        const auto obj = val.toObject();
        const auto formats = obj.value(QStringLiteral("media_formats")).toObject();

        GifItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.title = obj.value(QStringLiteral("title")).toString();
        item.contentDescription = obj.value(QStringLiteral("content_description")).toString();
        if (item.contentDescription.isEmpty())
            item.contentDescription = item.title;

        const QString itemUrl = obj.value(QStringLiteral("itemurl")).toString();
        const QString fallbackUrl = obj.value(QStringLiteral("url")).toString();
        item.url = !itemUrl.isEmpty() ? itemUrl : fallbackUrl;

        item.full = parseTenorMediaFormat(formats, {
            QStringLiteral("gif"),
            QStringLiteral("mediumgif"),
            QStringLiteral("tinygif"),
        });
        item.preview = parseTenorMediaFormat(formats, {
            QStringLiteral("tinygif"),
            QStringLiteral("nanogif"),
            QStringLiteral("gif"),
        });
        item.tinygif = item.preview;
        item.mp4 = parseTenorMediaFormat(formats, {
            QStringLiteral("mp4"),
            QStringLiteral("loopedmp4"),
        });

        if (!item.preview.url.isValid())
            item.preview = item.full;
        if (!item.tinygif.url.isValid())
            item.tinygif = item.full;

        if (item.url.isEmpty() && item.full.url.isValid())
            item.url = item.full.url.toString();

        if (item.full.url.isValid() || item.preview.url.isValid() || item.tinygif.url.isValid())
            items.append(item);
    }

    return items;
}

QList<GifCategory> parseTenorCategories(const QJsonArray &tags)
{
    QList<GifCategory> cats;
    cats.reserve(tags.size());

    for (const auto &val : tags) {
        const auto obj = val.toObject();
        GifCategory cat;
        cat.name = obj.value(QStringLiteral("searchterm")).toString();
        if (cat.name.isEmpty())
            cat.name = obj.value(QStringLiteral("name")).toString();
        cat.searchTerm = obj.value(QStringLiteral("searchterm")).toString();
        if (cat.searchTerm.isEmpty())
            cat.searchTerm = cat.name;
        cat.thumbnailUrl = QUrl(obj.value(QStringLiteral("image")).toString());
        if (!cat.name.isEmpty())
            cats.append(cat);
    }

    return cats;
}

QList<GifCategory> klipyCategories()
{
    static const QStringList categories = {
        QStringLiteral("trending"),
        QStringLiteral("funny"),
        QStringLiteral("reaction"),
        QStringLiteral("meme"),
        QStringLiteral("happy"),
        QStringLiteral("sad"),
        QStringLiteral("love"),
        QStringLiteral("dance"),
        QStringLiteral("anime"),
        QStringLiteral("gaming"),
    };

    QList<GifCategory> cats;
    cats.reserve(categories.size());
    for (const auto &name : categories) {
        GifCategory cat;
        cat.name = name;
        cat.searchTerm = name;
        cats.append(cat);
    }
    return cats;
}

QList<GifItem> parseKlipyResults(const QJsonValue &value)
{
    const QJsonArray items = value.isArray() ? value.toArray()
                                             : value.toObject().value(QStringLiteral("data")).toArray();
    QList<GifItem> results;
    results.reserve(items.size());

    for (const auto &val : items) {
        const auto obj = val.toObject();
        const auto file = obj.value(QStringLiteral("file")).toObject();
        const auto hd = file.value(QStringLiteral("hd")).toObject();
        const auto md = file.value(QStringLiteral("md")).toObject();
        const auto sm = file.value(QStringLiteral("sm")).toObject();
        const auto xs = file.value(QStringLiteral("xs")).toObject();

        const auto hdGif = hd.value(QStringLiteral("gif")).toObject();
        const auto smGif = sm.value(QStringLiteral("gif")).toObject();
        const auto xsGif = xs.value(QStringLiteral("gif")).toObject();
        const QString hdUrl = hdGif.value(QStringLiteral("url")).toString();
        const QString smUrl = smGif.value(QStringLiteral("url")).toString();
        const QString xsUrl = xsGif.value(QStringLiteral("url")).toString();
        const QString fullUrl = !hdUrl.isEmpty() ? hdUrl : (!smUrl.isEmpty() ? smUrl : xsUrl);

        GifItem item;
        item.id = obj.value(QStringLiteral("id")).toVariant().toString();
        if (item.id.isEmpty())
            item.id = obj.value(QStringLiteral("slug")).toString();
        item.title = obj.value(QStringLiteral("title")).toString();
        item.contentDescription = item.title;
        item.url = fullUrl;
        item.full.url = QUrl(item.url);
        const int hdWidth = hdGif.value(QStringLiteral("width")).toInt();
        const int hdHeight = hdGif.value(QStringLiteral("height")).toInt();
        const int smWidth = smGif.value(QStringLiteral("width")).toInt();
        const int smHeight = smGif.value(QStringLiteral("height")).toInt();
        item.full.dimensions = QSize(hdWidth > 0 ? hdWidth : smWidth,
                                     hdHeight > 0 ? hdHeight : smHeight);
        item.preview.url = QUrl(!smUrl.isEmpty() ? smUrl : xsUrl);
        if (!item.preview.url.isValid())
            item.preview.url = item.full.url;
        item.tinygif = item.preview;
        item.mp4.url = QUrl(sm.value(QStringLiteral("mp4")).toObject().value(QStringLiteral("url")).toString());
        if (!item.mp4.url.isValid())
            item.mp4.url = QUrl(sm.value(QStringLiteral("webm")).toObject().value(QStringLiteral("url")).toString());
        if (!item.mp4.url.isValid())
            item.mp4.url = QUrl(md.value(QStringLiteral("mp4")).toObject().value(QStringLiteral("url")).toString());

        if (!item.url.isEmpty() || item.preview.url.isValid() || item.full.url.isValid())
            results.append(item);
    }

    return results;
}
} // namespace

GifProvider::GifProvider(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_provider(defaultProviderFromSettings())
{
}

GifProvider::~GifProvider()
{
    cancelAll();
}

QString GifProvider::apiKey()
{
    return apiKey(Provider::Giphy);
}

QString GifProvider::apiKey(Provider provider)
{
    QSettings settings;
    QString key;
    switch (provider) {
    case Provider::Giphy:
        key = settings.value(QString::fromLatin1(kGiphySettingsKey)).toString();
        if (key.isEmpty()) {
            qCWarning(LogDiscord)
                    << "No GIPHY API key configured. "
                    << "Set giphy/apiKey in settings or get a free key at https://developers.giphy.com/";
        }
        break;
    case Provider::Klipy:
        key = settings.value(QString::fromLatin1(kKlipySettingsKey)).toString();
        if (key.isEmpty()) {
            qCWarning(LogDiscord)
                    << "No Klipy API key configured. "
                    << "Set klipy/apiKey in settings or get a key at https://klipy.com/developers/";
        }
        break;
    case Provider::Tenor:
        key = settings.value(QString::fromLatin1(kTenorApiKeySettingsKey)).toString();
        if (key.isEmpty()) {
            qCWarning(LogDiscord)
                    << "No Tenor API key configured. "
                    << "Set tenor/apiKey in settings or get a free key at https://developers.tenor.com/";
        }
        break;
    }
    return key;
}

GifProvider::Provider GifProvider::providerFromSettings(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("tenor_web") || normalized == QStringLiteral("tenor"))
        return Provider::Tenor;
    if (normalized == QStringLiteral("giphy"))
        return Provider::Giphy;
    if (normalized == QStringLiteral("klipy"))
        return Provider::Klipy;
    return Provider::Tenor;
}

QString GifProvider::providerToSettings(Provider provider)
{
    switch (provider) {
    case Provider::Giphy:
        return QStringLiteral("giphy");
    case Provider::Klipy:
        return QStringLiteral("klipy");
    case Provider::Tenor:
    default:
        return QStringLiteral("tenor");
    }
}

QString GifProvider::providerDisplayName(Provider provider)
{
    switch (provider) {
    case Provider::Giphy:
        return QStringLiteral("GIPHY");
    case Provider::Klipy:
        return QStringLiteral("Klipy");
    case Provider::Tenor:
    default:
        return QStringLiteral("Tenor");
    }
}

bool GifProvider::providerNeedsKey(Provider provider)
{
    // Tenor (v2 API) also requires an API key on every request — without it
    // every call 400s and the picker silently shows "No GIFs found" for a
    // configuration error, with no warning pointing the user at the key.
    return provider == Provider::Giphy || provider == Provider::Klipy ||
           provider == Provider::Tenor;
}

GifProvider::Provider GifProvider::defaultProvider()
{
    return defaultProviderFromSettings();
}

void GifProvider::setProvider(Provider provider)
{
    if (m_provider == provider)
        return;

    cancelAll();
    m_provider = provider;
    m_nextPageToken.clear();
    m_nextOffset = 0;
    m_lastCount = 0;
    m_lastTotal = 0;

    QSettings settings;
    settings.setValue(QString::fromLatin1(kProviderSettingsKey), providerToSettings(provider));
}

void GifProvider::cancelAll()
{
    // Detach the set before iterating: reply->abort() emits finished()
    // synchronously, whose handler removes the reply from m_pending — erasing
    // the element the range-for iterator is about to advance past (UB on a
    // QSet). Iterating a moved-out copy keeps the handler's remove() harmless.
    const QSet<QNetworkReply *> pending = std::move(m_pending);
    m_pending.clear();
    for (auto *reply : pending) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
    }
}

void GifProvider::executeRequest(const QUrl &url,
                                  std::function<void(const QJsonDocument &)> callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    // A hung GIF backend must not wedge the picker: the dialog sets m_loading
    // before the request and only clears it in the callback, so without a
    // timeout every later search/loadMore would early-return forever.
    request.setTransferTimeout(15'000);
    QNetworkReply *reply = m_nam->get(request);
    m_pending.insert(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() {
        m_pending.remove(reply);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Log only the path, never the full URL: GIF provider API keys are
            // embedded in the query string and must not be written to logs.
            qCWarning(LogDiscord) << providerDisplayName(m_provider) << "API request failed:"
                                  << reply->errorString() << "path:" << reply->url().path();
            callback(QJsonDocument{});
            return;
        }

        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        callback(doc);
    });
}

void GifProvider::search(const QString &query, int offset, GifSearchCallback callback)
{
    switch (m_provider) {
    case Provider::Tenor: {
        if (offset == 0)
            m_nextPageToken.clear();

        QUrl url(QStringLiteral("%1/search").arg(QString::fromLatin1(kTenorBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), apiKey(Provider::Tenor));
        q.addQueryItem(QStringLiteral("client_key"), QString::fromLatin1(kTenorClientKey));
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        q.addQueryItem(QStringLiteral("contentfilter"), QStringLiteral("low"));
        if (offset > 0 && !m_nextPageToken.isEmpty())
            q.addQueryItem(QStringLiteral("pos"), m_nextPageToken);
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback), offset](const QJsonDocument &doc) {
            QList<GifItem> items;
            bool hasMore = false;

            if (doc.isObject()) {
                const auto obj = doc.object();
                m_nextPageToken = obj.value(QStringLiteral("next")).toString();
                items = parseTenorResults(obj.value(QStringLiteral("results")).toArray());
                m_nextOffset = offset + items.size();
                // hasMore is driven by the next-page token alone: a valid-but-
                // empty page (rate limit, filtered results) must not silently
                // stop pagination just because it returned zero items.
                hasMore = !m_nextPageToken.isEmpty();
            }

            cb(items, hasMore);
        });
        break;
    }
    case Provider::Giphy: {
        QUrl url(QStringLiteral("%1/search").arg(QString::fromLatin1(kGiphyBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), apiKey(Provider::Giphy));
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
        q.addQueryItem(QStringLiteral("rating"), QStringLiteral("g"));
        q.addQueryItem(QStringLiteral("lang"), QStringLiteral("en"));
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback)](const QJsonDocument &doc) {
            QList<GifItem> items;
            bool hasMore = false;

            if (doc.isObject()) {
                const auto obj = doc.object();
                const auto pagination = obj.value(QStringLiteral("pagination")).toObject();
                m_lastCount = pagination.value(QStringLiteral("count")).toInt();
                m_lastTotal = pagination.value(QStringLiteral("total_count")).toInt();
                m_nextOffset = pagination.value(QStringLiteral("offset")).toInt() + m_lastCount;
                items = parseResults(obj.value(QStringLiteral("data")).toArray());
                hasMore = m_nextOffset < m_lastTotal && !items.isEmpty();
                m_nextPageToken.clear();
            }

            cb(items, hasMore);
        });
        break;
    }
    case Provider::Klipy: {
        const QString key = apiKey(Provider::Klipy);
        if (key.isEmpty()) {
            callback(QList<GifItem>{}, false);
            return;
        }

        QUrl url(QStringLiteral("https://api.klipy.com/api/v1/%1/gifs/search").arg(key));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("q"), query);
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback)](const QJsonDocument &doc) {
            const auto obj = doc.isObject() ? doc.object() : QJsonObject{};
            QList<GifItem> items = parseKlipyResults(obj.value(QStringLiteral("data")));
            // This Klipy endpoint accepts no pagination parameter, so claiming
            // "has more" and advancing m_nextOffset would re-request the same
            // first page forever and fill the grid with duplicates. Report a
            // single page only.
            m_nextOffset = 0;
            m_nextPageToken.clear();
            cb(items, false);
        });
        break;
    }
    }
}

void GifProvider::trending(int offset, GifSearchCallback callback)
{
    switch (m_provider) {
    case Provider::Tenor: {
        if (offset == 0)
            m_nextPageToken.clear();

        QUrl url(QStringLiteral("%1/featured").arg(QString::fromLatin1(kTenorBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), apiKey(Provider::Tenor));
        q.addQueryItem(QStringLiteral("client_key"), QString::fromLatin1(kTenorClientKey));
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        q.addQueryItem(QStringLiteral("contentfilter"), QStringLiteral("low"));
        if (offset > 0 && !m_nextPageToken.isEmpty())
            q.addQueryItem(QStringLiteral("pos"), m_nextPageToken);
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback), offset](const QJsonDocument &doc) {
            QList<GifItem> items;
            bool hasMore = false;

            if (doc.isObject()) {
                const auto obj = doc.object();
                m_nextPageToken = obj.value(QStringLiteral("next")).toString();
                items = parseTenorResults(obj.value(QStringLiteral("results")).toArray());
                m_nextOffset = offset + items.size();
                // hasMore is driven by the next-page token alone: a valid-but-
                // empty page (rate limit, filtered results) must not silently
                // stop pagination just because it returned zero items.
                hasMore = !m_nextPageToken.isEmpty();
            }

            cb(items, hasMore);
        });
        break;
    }
    case Provider::Giphy: {
        QUrl url(QStringLiteral("%1/trending").arg(QString::fromLatin1(kGiphyBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), apiKey(Provider::Giphy));
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        q.addQueryItem(QStringLiteral("offset"), QString::number(offset));
        q.addQueryItem(QStringLiteral("rating"), QStringLiteral("g"));
        q.addQueryItem(QStringLiteral("lang"), QStringLiteral("en"));
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback)](const QJsonDocument &doc) {
            QList<GifItem> items;
            bool hasMore = false;

            if (doc.isObject()) {
                const auto obj = doc.object();
                const auto pagination = obj.value(QStringLiteral("pagination")).toObject();
                m_lastCount = pagination.value(QStringLiteral("count")).toInt();
                m_lastTotal = pagination.value(QStringLiteral("total_count")).toInt();
                m_nextOffset = pagination.value(QStringLiteral("offset")).toInt() + m_lastCount;
                items = parseResults(obj.value(QStringLiteral("data")).toArray());
                hasMore = m_nextOffset < m_lastTotal && !items.isEmpty();
                m_nextPageToken.clear();
            }

            cb(items, hasMore);
        });
        break;
    }
    case Provider::Klipy: {
        const QString key = apiKey(Provider::Klipy);
        if (key.isEmpty()) {
            callback(QList<GifItem>{}, false);
            return;
        }

        QUrl url(QStringLiteral("https://api.klipy.com/api/v1/%1/gifs/trending").arg(key));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("limit"), QString::number(kDefaultLimit));
        url.setQuery(q);

        executeRequest(url, [this, cb = std::move(callback)](const QJsonDocument &doc) {
            const auto obj = doc.isObject() ? doc.object() : QJsonObject{};
            QList<GifItem> items = parseKlipyResults(obj.value(QStringLiteral("data")));
            // Single page only: no pagination parameter is supported by this
            // endpoint, so offset-based loading would duplicate the first page.
            m_nextOffset = 0;
            m_nextPageToken.clear();
            cb(items, false);
        });
        break;
    }
    }
}

void GifProvider::categories(GifCategoriesCallback callback)
{
    switch (m_provider) {
    case Provider::Tenor: {
        QUrl url(QStringLiteral("%1/categories").arg(QString::fromLatin1(kTenorBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), apiKey(Provider::Tenor));
        q.addQueryItem(QStringLiteral("client_key"), QString::fromLatin1(kTenorClientKey));
        q.addQueryItem(QStringLiteral("contentfilter"), QStringLiteral("low"));
        url.setQuery(q);

        executeRequest(url, [cb = std::move(callback)](const QJsonDocument &doc) {
            QList<GifCategory> cats;
            if (doc.isObject()) {
                cats = parseTenorCategories(doc.object().value(QStringLiteral("tags")).toArray());
            }
            cb(cats);
        });
        break;
    }
    case Provider::Giphy: {
        QUrl url(QStringLiteral("%1/categories").arg(QString::fromLatin1(kGiphyBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), apiKey(Provider::Giphy));
        url.setQuery(q);

        executeRequest(url, [cb = std::move(callback)](const QJsonDocument &doc) {
            QList<GifCategory> cats;
            if (doc.isObject()) {
                cats = parseCategories(doc.object().value(QStringLiteral("data")).toArray());
            }
            cb(cats);
        });
        break;
    }
    case Provider::Klipy: {
        callback(klipyCategories());
        break;
    }
    }
}

void GifProvider::suggestions(const QString &partial,
                               std::function<void(const QStringList &)> callback)
{
    switch (m_provider) {
    case Provider::Tenor: {
        QUrl url(QStringLiteral("%1/search_suggestions").arg(QString::fromLatin1(kTenorBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("key"), apiKey(Provider::Tenor));
        q.addQueryItem(QStringLiteral("client_key"), QString::fromLatin1(kTenorClientKey));
        q.addQueryItem(QStringLiteral("q"), partial);
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
        q.addQueryItem(QStringLiteral("contentfilter"), QStringLiteral("low"));
        url.setQuery(q);

        executeRequest(url, [cb = std::move(callback)](const QJsonDocument &doc) {
            QStringList results;
            if (doc.isObject()) {
                const auto data = doc.object().value(QStringLiteral("results")).toArray();
                for (const auto &val : data) {
                    const auto obj = val.toObject();
                    results << obj.value(QStringLiteral("searchterm")).toString();
                }
            }
            cb(results);
        });
        break;
    }
    case Provider::Giphy: {
        QUrl url(QStringLiteral("%1/search/tags").arg(QString::fromLatin1(kGiphyBase)));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("api_key"), apiKey(Provider::Giphy));
        q.addQueryItem(QStringLiteral("q"), partial);
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("10"));
        url.setQuery(q);

        executeRequest(url, [cb = std::move(callback)](const QJsonDocument &doc) {
            QStringList results;
            if (doc.isObject()) {
                const auto data = doc.object().value(QStringLiteral("data")).toArray();
                for (const auto &val : data) {
                    const auto obj = val.toObject();
                    results << obj.value(QStringLiteral("name")).toString();
                }
            }
            cb(results);
        });
        break;
    }
    case Provider::Klipy:
        callback(QStringList{});
        break;
    }
}

void GifProvider::recordSelection(const GifItem &item, const QString &query)
{
    if (m_provider != Provider::Tenor)
        return;
    if (item.id.isEmpty() || query.trimmed().isEmpty())
        return;

    QUrl url(QStringLiteral("%1/registershare").arg(QString::fromLatin1(kTenorBase)));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("key"), apiKey(Provider::Tenor));
    q.addQueryItem(QStringLiteral("client_key"), QString::fromLatin1(kTenorClientKey));
    q.addQueryItem(QStringLiteral("id"), item.id);
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    QNetworkReply *reply = m_nam->get(request);
    m_pending.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_pending.remove(reply);
        reply->deleteLater();
    });
}

GifMediaFormat GifProvider::parseMediaFormat(const QJsonObject &images,
                                              const QStringList &keys)
{
    GifMediaFormat result;
    for (const auto &key : keys) {
        auto fmt = images.value(key).toObject();
        if (fmt.isEmpty())
            continue;
        result.url = QUrl(fmt.value(QStringLiteral("url")).toString());
        if (!result.url.isValid())
            continue;
        auto dims = fmt.value(QStringLiteral("width")).toString();
        auto dimh = fmt.value(QStringLiteral("height")).toString();
        bool okW = false, okH = false;
        int w = dims.toInt(&okW);
        int h = dimh.toInt(&okH);
        if (okW && okH)
            result.dimensions = QSize(w, h);
        result.size = fmt.value(QStringLiteral("size")).toVariant().toLongLong();
        break; // Use first valid match
    }
    return result;
}

QList<GifItem> GifProvider::parseResults(const QJsonArray &results)
{
    QList<GifItem> items;
    items.reserve(results.size());

    for (const auto &val : results) {
        auto obj = val.toObject();
        GifItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.title = obj.value(QStringLiteral("title")).toString();
        item.contentDescription = obj.value(QStringLiteral("content_description")).toString();
        if (item.contentDescription.isEmpty())
            item.contentDescription = item.title;

        // Canonical GIPHY share URL
        item.url = QStringLiteral("https://giphy.com/gifs/%1").arg(item.id);

        auto images = obj.value(QStringLiteral("images")).toObject();

        // Tiny grid thumbnail: prefer downsized_small, fall back to fixed_width_small
        item.tinygif = parseMediaFormat(images, {
            QStringLiteral("fixed_width_small_still"),
            QStringLiteral("fixed_width_small"),
            QStringLiteral("downsized_still"),
            QStringLiteral("downsized"),
        });

        // Full-size GIF for sending: use original, fall back to fixed_width
        item.full = parseMediaFormat(images, {
            QStringLiteral("original"),
            QStringLiteral("fixed_width"),
            QStringLiteral("downsized"),
        });

        // Preview for hover animation: prefer preview_gif, fall back to fixed_width
        item.preview = parseMediaFormat(images, {
            QStringLiteral("preview_gif"),
            QStringLiteral("fixed_width"),
        });
        // If preview has no size, use the tinygif for preview too
        if (!item.preview.url.isValid())
            item.preview = item.tinygif;

        // MP4 version
        item.mp4 = parseMediaFormat(images, {
            QStringLiteral("original_mp4"),
            QStringLiteral("fixed_width_small_mp4"),
        });

        item.hasAudio = false;

        if (item.preview.url.isValid() || item.full.url.isValid() || item.tinygif.url.isValid())
            items.append(item);
    }

    return items;
}

QList<GifCategory> GifProvider::parseCategories(const QJsonArray &categories)
{
    QList<GifCategory> cats;
    cats.reserve(categories.size());

    for (const auto &val : categories) {
        auto obj = val.toObject();
        GifCategory cat;
        cat.searchTerm = obj.value(QStringLiteral("name_encoded")).toString();
        cat.name = obj.value(QStringLiteral("name")).toString();

        // Extract thumbnail from category GIF
        auto gifObj = obj.value(QStringLiteral("gif")).toObject();
        auto images = gifObj.value(QStringLiteral("images")).toObject();
        auto preview = images.value(QStringLiteral("preview_gif")).toObject();
        QString thumbUrl = preview.value(QStringLiteral("url")).toString();
        if (thumbUrl.isEmpty()) {
            auto fixed = images.value(QStringLiteral("fixed_width_small")).toObject();
            thumbUrl = fixed.value(QStringLiteral("url")).toString();
        }
        cat.thumbnailUrl = QUrl(thumbUrl);

        if (!cat.name.isEmpty())
            cats.append(cat);
    }

    return cats;
}

} // namespace Discord
} // namespace Acheron

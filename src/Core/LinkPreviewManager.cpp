#include "LinkPreviewManager.hpp"

#include "NetUtils.hpp"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

namespace Acheron {
namespace Core {

namespace {

bool looksLikeImagePath(const QUrl &url)
{
    const QString path = url.path().toLower();
    static const QStringList exts = {
        QStringLiteral(".gif"),  QStringLiteral(".png"),  QStringLiteral(".jpg"),
        QStringLiteral(".jpeg"), QStringLiteral(".webp"), QStringLiteral(".avif"),
        QStringLiteral(".bmp"),  QStringLiteral(".svg"),
    };
    for (const QString &ext : exts) {
        if (path.endsWith(ext))
            return true;
    }
    return false;
}

bool looksLikeVideoPath(const QUrl &url)
{
    const QString path = url.path().toLower();
    static const QStringList exts = {
        QStringLiteral(".mp4"),  QStringLiteral(".webm"), QStringLiteral(".mov"),
        QStringLiteral(".m4v"),  QStringLiteral(".mkv"),  QStringLiteral(".gifv"),
    };
    for (const QString &ext : exts) {
        if (path.endsWith(ext))
            return true;
    }
    return false;
}

/// Pull the `content` attribute of the meta tag declaring `property`, in
/// either attribute order. Returns an empty string when absent.
QString ogContent(const QByteArray &body, const QByteArray &property)
{
    // Build regexes per-property (cannot be static: static would freeze the
    // first property's interpolation and break og:description/image/video).
    // Escape property to avoid regex injection.
    const QString escaped = QRegularExpression::escape(QString::fromLatin1(property));
    const QRegularExpression propertyFirst(
        QStringLiteral(R"(<meta[^>]{0,400}?(?:property|name)=["']%1["'][^>]*?content=["']([^"']*)["'][^>]*>)")
            .arg(escaped),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression contentFirst(
        QStringLiteral(R"(<meta[^>]{0,400}?content=["']([^"']*)["'][^>]*?(?:property|name)=["']%1["'][^>]*>)")
            .arg(escaped),
        QRegularExpression::CaseInsensitiveOption);

    const QString text = QString::fromUtf8(body);
    const QRegularExpressionMatch m1 = propertyFirst.match(text);
    if (m1.hasMatch())
        return m1.captured(1).trimmed();
    const QRegularExpressionMatch m2 = contentFirst.match(text);
    if (m2.hasMatch())
        return m2.captured(1).trimmed();
    return {};
}

QString htmlUnescape(const QString &value)
{
    QString out = value;
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    return out.trimmed();
}

QUrl resolveMediaUrl(const QUrl &value, const QUrl &pageUrl)
{
    if (value.isEmpty())
        return {};
    QUrl url(value);
    if (url.isRelative())
        url = pageUrl.resolved(url);
    if (!url.scheme().startsWith(QStringLiteral("http")))
        return {};
    return url;
}

} // namespace

LinkPreviewManager::LinkPreviewManager(QObject *parent)
    : QObject(parent)
{
}

bool LinkPreviewManager::tryCached(const QUrl &url, LinkPreviewResult *out) const
{
    const auto it = m_cache.constFind(url);
    if (it == m_cache.constEnd())
        return false;
    if (out)
        *out = it.value();
    return true;
}

void LinkPreviewManager::requestPreview(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return;
    if (NetUtils::isPrivateHost(url.host())) {
        rememberFailure(url);
        return;
    }
    if (m_cache.contains(url) || m_failed.contains(url) || m_inFlight.contains(url) ||
        m_queue.contains(url))
        return;
    if (m_queue.size() >= kMaxQueue)
        return; // bounded to avoid DoS via channel with 10k links
    m_queue.append(url);
    processNext();
}

void LinkPreviewManager::processNext()
{
    if (!m_inFlight.isEmpty() || m_queue.isEmpty())
        return;

    const QUrl url = m_queue.takeFirst();
    m_inFlight.insert(url);

    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                                     "AppleWebKit/537.36 (KHTML, like Gecko) "
                                     "Chrome/124.0 Safari/537.36"));
    // A hung server must not stall the whole one-at-a-time unfurl queue
    // forever (m_inFlight would never clear and every later requestPreview
    // would silently no-op).
    request.setTransferTimeout(10'000);
    QNetworkReply *reply = m_nam->get(request);
    // Guard against oversized HTML buffering before finishReply truncates.
    // Only HTML/text payloads are buffered for parsing, so the cap must not
    // abort large media (images/videos routinely exceed it).
    const auto isHtmlContentType = [](QNetworkReply *r) {
        const QString ct = r->header(QNetworkRequest::ContentTypeHeader)
                                   .toString()
                                   .section(QLatin1Char(';'), 0, 0)
                                   .trimmed()
                                   .toLower();
        return ct.contains(QStringLiteral("html")) || ct.isEmpty();
    };
    connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, isHtmlContentType]() {
        if (!isHtmlContentType(reply))
            return;
        QVariant len = reply->header(QNetworkRequest::ContentLengthHeader);
        if (len.isValid() && len.toLongLong() > kMaxBodyBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, reply,
            [reply, isHtmlContentType](qint64 received, qint64) {
                if (received > kMaxBodyBytes && isHtmlContentType(reply))
                    reply->abort();
            });
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &redirectUrl) {
        if (NetUtils::isPrivateHost(redirectUrl.host())) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url]() { finishReply(reply, url); });
}

void LinkPreviewManager::finishReply(QNetworkReply *reply, const QUrl &requestedUrl)
{
    // Final SSRF check after redirects
    if (NetUtils::isPrivateHost(reply->url().host())) {
        reply->deleteLater();
        m_inFlight.remove(requestedUrl);
        rememberFailure(requestedUrl);
        processNext();
        return;
    }
    // Relative og: URLs must resolve against the FINAL post-redirect URL, not
    // the pre-redirect `requestedUrl` (a short link may land on another host
    // or a different path). reply->url() is the final document address.
    const QUrl finalUrl = reply->url();
    reply->deleteLater();
    m_inFlight.remove(requestedUrl);

    if (reply->error() != QNetworkReply::NoError) {
        rememberFailure(requestedUrl);
        processNext();
        return;
    }

    const QString contentType =
            reply->header(QNetworkRequest::ContentTypeHeader).toString();
    const QString ct = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();

    LinkPreviewResult out;
    out.filename = requestedUrl.fileName().isEmpty() ? requestedUrl.host()
                                                     : requestedUrl.fileName();
    out.mediaUrl = requestedUrl; // re-fetch goes through the redirect chain again (fresh)
    out.title = out.filename;
    out.contentType = ct;

    if (ct.startsWith(QStringLiteral("image/"))) {
        out.isImage = true;
        out.valid = true;
    } else if (ct.startsWith(QStringLiteral("video/"))) {
        out.isVideo = true;
        out.valid = true;
    } else if (ct == QStringLiteral("text/html") || ct.contains(QStringLiteral("html"))) {
        parseHtml(reply->read(kMaxBodyBytes), finalUrl, &out);
    } else if (ct.startsWith(QStringLiteral("application/octet-stream")) || ct.isEmpty()) {
        // Extension-less direct media (klipy-style) occasionally arrives as
        // octet-stream — or with NO content type at all (many CDNs omit it on
        // redirect targets). Fall back to path heuristics before attempting an
        // HTML parse, so binary bodies aren't fed to the og: tag regexes and
        // permanently marked failed.
        out.isImage = looksLikeImagePath(requestedUrl);
        out.isVideo = looksLikeVideoPath(requestedUrl);
        out.valid = out.isImage || out.isVideo;
        if (!out.valid)
            parseHtml(reply->read(kMaxBodyBytes), finalUrl, &out);
    }

    if (!out.valid) {
        rememberFailure(requestedUrl);
        processNext();
        return;
    }

    if (m_cache.size() >= kMaxCache)
        m_cache.erase(m_cache.begin());
    m_cache.insert(requestedUrl, out);

    emit previewReady(requestedUrl, out);
    processNext();
}

void LinkPreviewManager::parseHtml(const QByteArray &body, const QUrl &pageUrl,
                                   LinkPreviewResult *out)
{
    QString ogTitle = htmlUnescape(ogContent(body, "og:title"));
    const QString ogDescription = htmlUnescape(ogContent(body, "og:description"));
    const QString ogImage = htmlUnescape(ogContent(body, "og:image"));
    const QString ogVideo = htmlUnescape(ogContent(body, "og:video"));
    const QString ogVideoType = htmlUnescape(ogContent(body, "og:video:type"));

    // Fall back to twitter:title (many pages only expose twitter tags) and the
    // <title> element when og:title is absent.
    if (ogTitle.isEmpty()) {
        ogTitle = htmlUnescape(ogContent(body, "twitter:title"));
        if (ogTitle.isEmpty()) {
            static const QRegularExpression titleRe(
                    QStringLiteral(R"(<title[^>]*>([\s\S]*?)</title>)"),
                    QRegularExpression::CaseInsensitiveOption);
            const auto m = titleRe.match(QString::fromUtf8(body));
            if (m.hasMatch())
                ogTitle = m.captured(1).trimmed();
        }
    }

    if (!ogTitle.isEmpty())
        out->title = ogTitle;

    // Prefer a real video (og:video with a video content type / media path).
    const QUrl videoUrl = resolveMediaUrl(ogVideo, pageUrl);
    if (videoUrl.isValid() &&
        (ogVideoType.startsWith(QStringLiteral("video/")) || looksLikeVideoPath(videoUrl))) {
        out->isVideo = true;
        out->mediaUrl = videoUrl;
        out->thumbnailUrl = resolveMediaUrl(ogImage, pageUrl);
        out->valid = true;
        return;
    }

    const QUrl imageUrl = resolveMediaUrl(ogImage, pageUrl);
    if (imageUrl.isValid()) {
        out->isImage = true;
        out->mediaUrl = imageUrl;
        out->valid = true;
        return;
    }

    // No direct media to render; a title alone isn't worth a card.
    out->valid = false;
}

void LinkPreviewManager::rememberFailure(const QUrl &url)
{
    if (m_failed.size() >= kMaxFailed)
        m_failed.erase(m_failed.begin());
    m_failed.insert(url);
}

} // namespace Core
} // namespace Acheron

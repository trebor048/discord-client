#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace Acheron {
namespace Core {

/// Outcome of client-side link unfurling for a URL that Discord did not embed
/// (klipy/imgur-style short links that redirect to media without a file
/// extension). `mediaUrl` is the direct image/video URL to fetch or play.
struct LinkPreviewResult
{
    bool valid = false;
    bool isImage = false;
    bool isVideo = false;
    QString contentType;   // sniffed Content-Type of the resolved resource
    QUrl mediaUrl;       // direct media URL (image or video)
    QUrl thumbnailUrl;   // og:image for video pages; empty for raw media
    QString title;
    QString filename;
};

/// GETs the URL (QNetworkAccessManager follows redirects), sniffs the content
/// type, and for HTML pages parses Open Graph tags. Runs one request at a time
/// with a bounded queue; results and failures are cached for the session so
/// rows repaint without refetching.
class LinkPreviewManager : public QObject
{
    Q_OBJECT
public:
    explicit LinkPreviewManager(QObject *parent = nullptr);

    /// Enqueue a URL for unfurling. No-op when already cached, in flight,
    /// queued, or known-failed.
    void requestPreview(const QUrl &url);

    /// Cached result for a URL; false when unknown or failed.
    bool tryCached(const QUrl &url, LinkPreviewResult *out) const;

signals:
    void previewReady(const QUrl &url, const Core::LinkPreviewResult &result);

private:
    void processNext();
    void finishReply(QNetworkReply *reply, const QUrl &requestedUrl);
    void parseHtml(const QByteArray &body, const QUrl &pageUrl, LinkPreviewResult *out);
    void rememberFailure(const QUrl &url);

    QNetworkAccessManager *m_nam = nullptr;
    QList<QUrl> m_queue;
    QSet<QUrl> m_inFlight;
    QHash<QUrl, LinkPreviewResult> m_cache;
    QSet<QUrl> m_failed;

    static constexpr int kMaxCache = 256;
    static constexpr int kMaxFailed = 512;
    static constexpr int kMaxQueue = 128;
    static constexpr qint64 kMaxBodyBytes = 512 * 1024;
};

} // namespace Core
} // namespace Acheron

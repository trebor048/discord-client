#include "ImageManager.hpp"

#include <QThreadPool>

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QFile>
#include <QUrlQuery>
#include <QApplication>
#include <QImageReader>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "Logging.hpp"

namespace Acheron {
namespace Core {

// ---------------------------------------------------------------------------
// Animated image format detection
// ---------------------------------------------------------------------------

// Formats that can animate through QMovie when the runtime Qt imageformats
// plugins support them. Anything else (png, jpeg, ...) renders as a static
// image even if it is actually animated underneath.
static bool animatedFormatSupported(const QByteArray &format)
{
    static const QSet<QByteArray> animated = []() {
        QSet<QByteArray> supported;
        const QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for (const QByteArray &f : formats) {
            if (f == QByteArrayLiteral("gif") || f == QByteArrayLiteral("webp")
                || f == QByteArrayLiteral("avif")) {
                supported.insert(f);
            }
        }
        return supported;
    }();
    return animated.contains(format);
}

// Maps a content-type header / URL suffix to one of the animated-capable
// formats, or an empty QByteArray when neither names gif/webp/avif.
static QByteArray animatedFormatFrom(const QUrl &url, const QString &mime)
{
    const QString mt = mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    if (mt == QStringLiteral("image/gif"))
        return QByteArrayLiteral("gif");
    if (mt == QStringLiteral("image/webp"))
        return QByteArrayLiteral("webp");
    if (mt == QStringLiteral("image/avif"))
        return QByteArrayLiteral("avif");

    const QString ext = url.path().section(QLatin1Char('.'), -1).toLower();
    if (ext == QStringLiteral("gif") || ext == QStringLiteral("webp")
        || ext == QStringLiteral("avif")) {
        return ext.toUtf8();
    }
    return QByteArray();
}

// ---------------------------------------------------------------------------
// GifAnimation
// ---------------------------------------------------------------------------

GifAnimation::GifAnimation(QObject *parent)
    : QObject(parent)
{
}

void GifAnimation::load(const QUrl &url, int containerWidth)
{
    // Abort previous in-flight request if load is called again before it completes
    if (m_activeReply) {
        disconnect(m_activeReply, nullptr, this, nullptr);
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }

    m_url = url;
    m_containerWidth = qBound(32, containerWidth, kGifMaxWidth);
    m_loading = true;
    m_loadProgress = 0;

    // Lazily create the QNetworkAccessManager once and reuse it for
    // subsequent loads, instead of creating a new one per call.
    if (!m_nam)
        m_nam = new QNetworkAccessManager(this);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = m_nam->get(request);
    m_activeReply = reply;
    ImageManager::guardReply(reply);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0) {
                    m_loadProgress = qBound(0, int(received * 100 / total), 100);
                    emit loadProgressChanged(m_loadProgress);
                }
            });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_activeReply == reply)
            m_activeReply = nullptr;
        reply->deleteLater();
        m_loading = false;
        m_loadProgress = -1;

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(LogCore) << "Failed to fetch GIF:" << reply->errorString();
            emit failed();
            return;
        }

        QByteArray data = reply->readAll();
        const QString contentType =
                reply->header(QNetworkRequest::ContentTypeHeader).toString();

        // Only payloads whose URL/mime (or sniffed container) identify an
        // animated-capable format (.gif/.webp/.avif) with runtime plugin
        // support go through QMovie. Everything else — including animated
        // formats the deployed plugins cannot decode — renders statically.
        const bool animatedCapable =
                ImageManager::isSupportedAnimatedImage(m_url, contentType)
                || animatedFormatSupported(QImageReader::imageFormat(data));

        if (!animatedCapable) {
            QImage staticImage = QImage::fromData(data);
            if (staticImage.isNull()) {
                qCWarning(LogCore) << "Undecodable GIF/image data from" << m_url;
                emit failed();
                return;
            }
            m_staticImage = staticImage;
            m_displaySize = computeDisplaySize(staticImage.size());
            emit ready();
            return;
        }

        m_buffer = new QBuffer(this);
        m_buffer->setData(data);
        m_buffer->open(QIODevice::ReadOnly);

        m_movie = new QMovie(this);
        m_movie->setDevice(m_buffer);
        // Deliberately no setFormat(): sniff the actual container so animated
        // webp/avif (and any other supported format) isn't rejected as "not
        // gif". Klipy/media-proxy thumbnails are frequently served as webp.

        if (!m_movie->isValid()) {
            // The payload claims an animated-capable format but QMovie could
            // not decode it (e.g. an unsupported or corrupt container); it may
            // still be a static poster image (e.g. a static webp thumbnail for
            // a gifv embed). Show that instead of failing into a gray box.
            m_movie->deleteLater();
            m_movie = nullptr;
            m_buffer->deleteLater();
            m_buffer = nullptr;

            QImage staticImage = QImage::fromData(data);
            if (staticImage.isNull()) {
                qCWarning(LogCore) << "Undecodable GIF/image data from" << m_url;
                emit failed();
                return;
            }

            m_staticImage = staticImage;
            m_displaySize = computeDisplaySize(staticImage.size());
            emit ready();
            return;
        }

        m_displaySize = computeDisplaySize(m_movie->currentImage().size());
        m_movie->setScaledSize(m_displaySize);

        // Connect frame changes
        connect(m_movie, &QMovie::frameChanged, this, &GifAnimation::onMovieFrameChanged);

        emit ready();

        // If we were asked to play before data arrived, start now — unless the
        // user disabled GIF/webp autoplay ("ui/gifAutoplay"), in which case the
        // first frame is shown statically until playback is enabled.
        if (m_playing && ImageManager::gifAutoplayEnabled())
            m_movie->start();
    });
}

QSize GifAnimation::computeDisplaySize(const QSize &originalSize) const
{
    QSize size = originalSize.isEmpty() ? QSize(kGifMaxWidth, kGifMaxWidth) : originalSize;
    if (size.width() <= m_containerWidth)
        return size;
    return QSize(m_containerWidth, size.height() * m_containerWidth / size.width());
}

QPixmap GifAnimation::currentFrame() const
{
    if (!m_staticImage.isNull()) {
        if (m_staticScaled.isNull()) {
            const QImage scaled = m_staticImage.size() == m_displaySize
                    ? m_staticImage
                    : m_staticImage.scaled(m_displaySize, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
            m_staticScaled = QPixmap::fromImage(scaled);
        }
        return m_staticScaled;
    }

    if (m_movie) {
        // QMovie keeps the current frame decoded and, with setScaledSize(),
        // already downscaled — no extra frame cache is needed (and jumping
        // around to pre-decode frames disrupts smooth playback).
        QImage img = m_movie->currentImage();
        if (!img.isNull())
            return QPixmap::fromImage(img);
    }
    return QPixmap();
}

int GifAnimation::frameCount() const
{
    if (!m_staticImage.isNull())
        return 1;
    if (!m_movie)
        return 0;
    return m_movie->frameCount();
}

qint64 GifAnimation::memoryCost() const
{
    qint64 cost = m_buffer ? m_buffer->size() : 0;
    if (!m_staticImage.isNull())
        cost += qint64(m_staticImage.width()) * m_staticImage.height() * 4;
    return cost;
}

void GifAnimation::unload()
{
    // Free the heavy buffers but keep the object alive so external raw
    // pointers (e.g. ChatModel's per-URL map) stay valid; play() reloads.
    pause();
    if (m_movie) {
        m_movie->deleteLater();
        m_movie = nullptr;
    }
    if (m_buffer) {
        m_buffer->deleteLater();
        m_buffer = nullptr;
    }
    m_staticImage = QImage();
    m_staticScaled = QPixmap();
}

void GifAnimation::play()
{
    m_playing = true;
    if (!m_movie && m_staticImage.isNull() && !m_loading && m_url.isValid()) {
        // Previously unloaded by the ImageManager LRU — fetch again.
        load(m_url, m_containerWidth);
        return;
    }
    // Honor the autoplay setting: with "ui/gifAutoplay" off the movie stays on
    // its first frame until the setting is re-enabled (play() is re-invoked by
    // views on every repaint, so flipping the setting back on resumes it).
    if (m_movie && ImageManager::gifAutoplayEnabled())
        m_movie->start();
}

void GifAnimation::pause()
{
    m_playing = false;
    if (m_movie)
        m_movie->stop();
}

void GifAnimation::toggle()
{
    if (m_playing)
        pause();
    else
        play();
}

void GifAnimation::setContainerWidth(int width)
{
    m_containerWidth = qBound(32, width, kGifMaxWidth);

    if (!m_staticImage.isNull()) {
        QSize newSize = computeDisplaySize(m_staticImage.size());
        if (newSize != m_displaySize) {
            m_displaySize = newSize;
            m_staticScaled = QPixmap();
        }
        return;
    }

    if (!m_movie)
        return;

    QSize originalSize = m_movie->currentImage().size();
    if (originalSize.isEmpty())
        return;

    QSize newSize = computeDisplaySize(originalSize);

    if (newSize != m_displaySize) {
        m_displaySize = newSize;
        m_movie->setScaledSize(m_displaySize);
    }
}

void GifAnimation::onMovieFrameChanged(int frameNum)
{
    Q_UNUSED(frameNum);
    QPixmap frame = currentFrame();
    if (!frame.isNull())
        emit frameChanged(frame);
}

// ---------------------------------------------------------------------------
// ImageManager GIF methods
// ---------------------------------------------------------------------------

bool ImageManager::isSupportedAnimatedImage(const QUrl &url, const QString &mime)
{
    const QByteArray format = animatedFormatFrom(url, mime);
    return !format.isEmpty() && animatedFormatSupported(format);
}

bool ImageManager::gifAutoplayEnabled()
{
    return QSettings().value(QStringLiteral("ui/gifAutoplay"), true).toBool();
}

GifAnimation *ImageManager::createGifAnimation(const QUrl &url, int containerWidth)
{
    auto it = gifAnimations.constFind(url);
    if (it != gifAnimations.constEnd()) {
        touchGifAnimation(url);
        return it.value();
    }

    auto *anim = new GifAnimation(this);
    anim->load(url, containerWidth);
    gifAnimations.insert(url, anim);

    // Auto-cleanup when the animation is destroyed externally
    connect(anim, &QObject::destroyed, this, [this, url](QObject *) {
        gifAnimations.remove(url);
        auto lit = gifLruMap.find(url);
        if (lit != gifLruMap.end()) {
            gifLruOrder.erase(lit.value());
            gifLruMap.erase(lit);
        }
        recalcGifMemoryTotal();
    });

    // Keep the running memory total in sync when the load finishes or fails.
    connect(anim, &GifAnimation::ready, this, &ImageManager::recalcGifMemoryTotal);
    connect(anim, &GifAnimation::failed, this, &ImageManager::recalcGifMemoryTotal);

    touchGifAnimation(url);
    enforceGifCacheLimits(url);

    return anim;
}

GifAnimation *ImageManager::gifAnimation(const QUrl &url) const
{
    auto *anim = gifAnimations.value(url, nullptr);
    if (anim)
        touchGifAnimation(url);
    return anim;
}

void ImageManager::releaseGifAnimation(const QUrl &url)
{
    auto it = gifAnimations.find(url);
    if (it != gifAnimations.end()) {
        it.value()->deleteLater();
        gifAnimations.erase(it);
        auto lit = gifLruMap.find(url);
        if (lit != gifLruMap.end()) {
            gifLruOrder.erase(lit.value());
            gifLruMap.erase(lit);
        }
        recalcGifMemoryTotal();
    }
}

void ImageManager::touchGifAnimation(const QUrl &url) const
{
    auto it = gifLruMap.find(url);
    if (it != gifLruMap.end()) {
        // Move to back (most recently used).
        gifLruOrder.splice(gifLruOrder.end(), gifLruOrder, it.value());
    } else {
        gifLruOrder.push_back(url);
        gifLruMap.insert(url, std::prev(gifLruOrder.end()));
    }
}

void ImageManager::recalcGifMemoryTotal()
{
    qint64 total = 0;
    for (auto *anim : gifAnimations)
        total += anim->memoryCost();
    gifMemoryTotal = total;
}

void ImageManager::enforceGifCacheLimits(const QUrl &mostRecent)
{
    recalcGifMemoryTotal();

    // Unload least-recently-used animations until back under budget. Unloading
    // frees the buffers but keeps the GifAnimation alive (play() reloads on
    // demand), so holders of raw pointers never dangle.
    while (gifLruOrder.size() > 1
           && (gifAnimations.size() > kGifCacheMaxEntries
               || gifMemoryTotal > kGifCacheBudgetBytes)) {
        const QUrl victim = gifLruOrder.front();
        auto lit = gifLruMap.find(victim);
        if (lit == gifLruMap.end()) {
            gifLruOrder.pop_front();
            continue;
        }
        if (victim == mostRecent) {
            gifLruOrder.splice(gifLruOrder.end(), gifLruOrder, lit.value());
            break;
        }
        auto *anim = gifAnimations.value(victim, nullptr);
        if (!anim) {
            gifLruMap.erase(lit);
            gifLruOrder.pop_front();
            continue;
        }
        const qint64 cost = anim->memoryCost();
        if (cost == 0) {
            // Already an empty shell; unloading again won't shrink anything.
            gifLruOrder.splice(gifLruOrder.end(), gifLruOrder, lit.value());
            if (gifMemoryTotal <= kGifCacheBudgetBytes)
                break;
            continue;
        }
        anim->unload();
        gifMemoryTotal -= cost;
        gifLruOrder.splice(gifLruOrder.end(), gifLruOrder, lit.value());
    }
}

// ---------------------------------------------------------------------------
// ImageManager (existing methods below)
// ---------------------------------------------------------------------------

ImageManager::ImageManager(QObject *parent) : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    // QCache cost is per-entry; we budget in decoded bytes (32bpp).
    cache.setMaxCost(kRamCacheBudgetBytes);

    if (!tempDir.isValid())
        qCWarning(LogCore) << "Failed to create temp directory for image cache";
}

bool ImageManager::isCached(const QUrl &url, const QSize &size)
{
    ImageRequestKey k{ url, size };
    if (pinnedImages.contains(k) || cache.contains(k) || diskCacheKeys.contains(k))
        return true;

    return false;
}

void ImageManager::assign(QLabel *label, const QUrl &url, const QSize &size)
{
    if (!label)
        return;

    // just in case
    disconnect(this, &ImageManager::imageFetched, label, nullptr);

    QPixmap pixmap = get(url, size);
    label->setPixmap(pixmap);

    if (!isCached(url, size)) {
        connect(this, &ImageManager::imageFetched, label,
                [=](const QUrl &u, const QSize &s, const QPixmap &p) {
                    if (u == url && s == size)
                        label->setPixmap(p);
                });
    }
}

QPixmap ImageManager::get(const QUrl &url, const QSize &size, PinGroup pin)
{
    return getImpl(url, size, pin, true);
}

QPixmap ImageManager::getIfCached(const QUrl &url, const QSize &size, PinGroup pin)
{
    return getImpl(url, size, pin, false);
}

QPixmap ImageManager::getImpl(const QUrl &url, const QSize &size, PinGroup pin, bool fetchIfNeeded)
{
    ImageRequestKey k{ url, size };

    auto pinnedIt = pinnedImages.constFind(k);
    if (pinnedIt != pinnedImages.constEnd()) {
        if (pin != PinGroup::None && !pinGroupKeys.contains(pin, k)) {
            pinGroupKeys.insert(pin, k);
            ++pinRefCounts[k];
        }
        return pinnedIt.value();
    }

    if (cache.contains(k)) {
        QPixmap pixmap = *cache.object(k);
        if (pin != PinGroup::None) {
            pinnedImages.insert(k, pixmap);
            pinGroupKeys.insert(pin, k);
            ++pinRefCounts[k];
            cache.remove(k);
        }
        return pixmap;
    }

    // check disk cache
    if (diskCacheKeys.contains(k)) {
        QString path = getCachePath(url, size);
        QPixmap pixmap;
        if (pixmap.load(path)) {
            qreal dpr = qApp->devicePixelRatio();
            bool proxy = isDiscordProxyUrl(url);

            if (proxy) {
                QSize physicalSize(qRound(size.width() * dpr), qRound(size.height() * dpr));
                if (pixmap.size() != physicalSize)
                    pixmap = pixmap.scaled(physicalSize, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
                pixmap.setDevicePixelRatio(dpr);
            } else {
                if (pixmap.size() != size)
                    pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }

            if (pin != PinGroup::None) {
                pinnedImages.insert(k, pixmap);
                pinGroupKeys.insert(pin, k);
                ++pinRefCounts[k];
            } else {
                cache.insert(k, new QPixmap(pixmap), pixmapCost(pixmap));
            }
            return pixmap;
        }

        // The file was removed or corrupted; drop the stale key.
        diskCacheKeys.remove(k);
        diskCachePathToKey.remove(path);
    }

    if (fetchIfNeeded) {
        request(url, size, pin);
    }

    return placeholder(size);
}

QPixmap ImageManager::placeholder(const QSize &size)
{
    qreal dpr = qApp->devicePixelRatio();
    QSize physicalSize(qRound(size.width() * dpr), qRound(size.height() * dpr));
    QPixmap pixmap(physicalSize);
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(QColor(60, 60, 60));
    return pixmap;
}

bool ImageManager::hasFailed(const QUrl &url, const QSize &size) const
{
    return failedRequests.contains(ImageRequestKey{ url, size });
}

void ImageManager::clearFailedRequests()
{
    failedRequests.clear();
}

void ImageManager::failRequest(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                               const QString &reason)
{
    qCWarning(LogCore) << "Failed to fetch image:" << url << "-" << reason;
    requests.remove(k);
    pendingPins.remove(k);
    failedRequests.insert(k);
    emit imageFailed(url, size);
}

void ImageManager::request(const QUrl &url, const QSize &size, PinGroup pin)
{
    ImageRequestKey k{ url, size };

    // A failed request stays failed (the delegate paints an error state)
    // instead of being re-issued on every repaint until explicitly cleared.
    if (failedRequests.contains(k))
        return;

    if (requests.contains(k)) {
        // promote
        if (pin != PinGroup::None) {
            auto it = pendingPins.find(k);
            if (it == pendingPins.end() || it.value() == PinGroup::None)
                pendingPins.insert(k, pin);
        }
        return;
    }

    requests.insert(k);
    if (pin != PinGroup::None)
        pendingPins.insert(k, pin);

    fetchFromNetwork(url, size, pin);
}

void ImageManager::fetchFromNetwork(const QUrl &url, const QSize &size, PinGroup pin)
{
    qreal dpr = qApp->devicePixelRatio();
    bool proxy = isDiscordProxyUrl(url);

    QUrl fetchUrl = proxy ? buildOptimizedUrl(url, size, dpr) : url;
    QNetworkRequest request(fetchUrl);
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = networkManager->get(request);
    guardReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, size, proxy, dpr]() {
        ImageRequestKey k{ url, size };
        PinGroup pin = pendingPins.value(k, PinGroup::None);
        // Keep pendingPins until the fallback chain is fully resolved so that
        // overlapping requests don't lose their pin promotion.

        if (reply->error() != QNetworkReply::NoError) {
            const QString reason = reply->errorString();
            reply->deleteLater();

            // The optimized URL (extra query params) may be rejected by the
            // proxy; retry the raw signed proxy URL before giving up.
            if (proxy) {
                fetchRawProxyUrl(k, url, size, dpr);
                return;
            }
            failRequest(k, url, size, reason);
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        QPixmap pixmap;
        if (pixmap.loadFromData(data)) {
            storeFetchedPixmap(k, url, size, data, pixmap, pin, proxy, dpr);
            return;
        }

        // The optimized URL (format/width params) was rejected or undecodable.
        // Fall back to the raw proxy URL, which Discord serves without resize
        // params (this is what the full-image viewer uses successfully).
        if (proxy) {
            fetchRawProxyUrl(k, url, size, dpr);
            return;
        }
        failRequest(k, url, size, QStringLiteral("undecodable image data"));
    });
}

void ImageManager::fetchRawProxyUrl(const ImageRequestKey &k, const QUrl &url,
                                    const QSize &size, qreal dpr)
{
    QNetworkRequest rawRequest(url);
    rawRequest.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *rawReply = networkManager->get(rawRequest);
    guardReply(rawReply);
    connect(rawReply, &QNetworkReply::finished, this,
            [this, rawReply, k, url, size, dpr]() {
        // Read the latest pin promotion (it may have been upgraded by an
        // overlapping request while the fallback fetch was in flight).
        PinGroup pin = pendingPins.value(k, PinGroup::None);

        if (rawReply->error() != QNetworkReply::NoError) {
            const QString reason = rawReply->errorString();
            rawReply->deleteLater();
            failRequest(k, url, size, reason);
            return;
        }
        QByteArray rawData = rawReply->readAll();
        rawReply->deleteLater();
        QPixmap raw;
        if (raw.loadFromData(rawData)) {
            storeFetchedPixmap(k, url, size, rawData, raw, pin, true, dpr);
            return;
        }
        failRequest(k, url, size, QStringLiteral("undecodable image data"));
    });
}

void ImageManager::guardReply(QNetworkReply *reply)
{
    // Abort oversized downloads instead of buffering them unboundedly: check
    // the advertised Content-Length as soon as headers arrive, then the
    // accumulated bytes on every read.
    QObject::connect(reply, &QNetworkReply::metaDataChanged, reply, [reply]() {
        const QVariant len = reply->header(QNetworkRequest::ContentLengthHeader);
        if (len.isValid() && len.toLongLong() > kMaxDownloadBytes)
            reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->size() > kMaxDownloadBytes)
            reply->abort();
    });
}

void ImageManager::pruneDiskCache()
{
    if (!tempDir.isValid())
        return;

    // Bound the session disk cache: delete oldest files until back under the
    // byte and file-count budgets.
    const QDir dir(tempDir.path());
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time); // oldest first

    qint64 totalBytes = 0;
    for (const QFileInfo &fi : files)
        totalBytes += fi.size();

    int count = files.size();
    for (const QFileInfo &fi : files) {
        if (totalBytes <= kDiskCacheBudgetBytes && count <= kDiskCacheMaxFiles)
            break;
        const QString path = fi.absoluteFilePath();
        if (QFile::remove(path)) {
            totalBytes -= fi.size();
            --count;
            auto it = diskCachePathToKey.find(path);
            if (it != diskCachePathToKey.end()) {
                diskCacheKeys.remove(it.value());
                diskCachePathToKey.erase(it);
            }
        }
    }
}
void ImageManager::storeFetchedPixmap(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                                      const QByteArray &data, QPixmap pixmap, PinGroup pin, bool proxy,
                                      qreal dpr)
{
    if (proxy) {
        QSize physicalSize(qRound(size.width() * dpr), qRound(size.height() * dpr));
        if (pixmap.size() != physicalSize)
            pixmap = pixmap.scaled(physicalSize, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
    } else {
        pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    if (pin != PinGroup::None) {
        pinnedImages.insert(k, pixmap);
        pinGroupKeys.insert(pin, k);
        ++pinRefCounts[k];
    } else {
        cache.insert(k, new QPixmap(pixmap), pixmapCost(pixmap));
    }

    requests.remove(k);
    pendingPins.remove(k);
    failedRequests.remove(k);
    emit imageFetched(url, size, pixmap);

    // Persist to disk asynchronously so the network/UI thread isn't blocked by
    // large writes. The in-memory cache is already live.
    const QString cachePath = getCachePath(url, size);
    QPointer<ImageManager> guard(this);
    QThreadPool::globalInstance()->start([guard, k, cachePath, data]() {
        QFile file(cachePath);
        bool ok = false;
        if (file.open(QIODevice::WriteOnly)) {
            ok = file.write(data) == data.size();
            file.close();
        }
        if (guard) {
            QMetaObject::invokeMethod(guard, [guard, k, cachePath, ok]() {
                guard->onDiskCacheWritten(k, cachePath, ok);
            }, Qt::QueuedConnection);
        }
    });
}

void ImageManager::onDiskCacheWritten(const ImageRequestKey &k, const QString &path, bool ok)
{
    if (!ok)
        return;

    diskCacheKeys.insert(k);
    diskCachePathToKey.insert(path, k);

    // Pruning scans the whole temp dir, so only do it periodically.
    if (++storesSincePrune >= 64) {
        storesSincePrune = 0;
        pruneDiskCache();
    }
}

void ImageManager::unpinGroup(PinGroup group)
{
    if (group == PinGroup::None)
        return;

    QList<ImageRequestKey> keys = pinGroupKeys.values(group);
    pinGroupKeys.remove(group);

    for (const auto &k : keys) {
        auto refIt = pinRefCounts.find(k);
        if (refIt == pinRefCounts.end())
            continue;

        if (--refIt.value() > 0)
            continue;

        // No remaining groups reference this key; move it back to the LRU.
        pinRefCounts.erase(refIt);
        auto it = pinnedImages.find(k);
        if (it != pinnedImages.end()) {
            cache.insert(k, new QPixmap(it.value()), pixmapCost(it.value()));
            pinnedImages.erase(it);
        }
    }
}

QSize ImageManager::calculateDisplaySize(const QSize &original)
{
    if (!original.isValid() || original.isEmpty())
        return QSize(MaxDisplayWidth, MaxDisplayHeight);

    if (original.width() <= MaxDisplayWidth && original.height() <= MaxDisplayHeight)
        return original;

    return original.scaled(MaxDisplayWidth, MaxDisplayHeight, Qt::KeepAspectRatio);
}

QString ImageManager::getCachePath(const QUrl &url, const QSize &size) const
{
    QString compound = url.toString() + QStringLiteral(":%1x%2").arg(size.width()).arg(size.height());
    QByteArray hash =
            QCryptographicHash::hash(compound.toUtf8(), QCryptographicHash::Sha1);
    QString filename = QString::fromLatin1(hash.toHex());
    return tempDir.filePath(filename);
}

bool ImageManager::isDiscordProxyUrl(const QUrl &url)
{
    QString host = url.host();
    return host == u"media.discordapp.net" || host.startsWith(u"images-ext-");
}

QUrl ImageManager::buildOptimizedUrl(const QUrl &proxyUrl, const QSize &displaySize, qreal dpr)
{
    QUrl optimized = proxyUrl;
    QUrlQuery query(optimized);

    // Avoid stacking duplicate keys if the caller already supplied them.
    query.removeQueryItem(QStringLiteral("quality"));
    query.removeQueryItem(QStringLiteral("width"));

    // Request the media proxy's original format instead of forcing webp. Hard
    // coding format=webp broke animated GIFs (and any content the deployed
    // runtime cannot decode as webp). The proxy serves the source format, and
    // fetchFromNetwork() still falls back to the raw proxy URL if decoding
    // fails, so both static images and animated GIFs render.
    // Lossy quality still keeps thumbnails small.
    query.addQueryItem(QStringLiteral("quality"), QStringLiteral("80"));

    if (displaySize.isValid() && !displaySize.isEmpty()) {
        int physicalWidth = qRound(displaySize.width() * dpr);
        query.addQueryItem(QStringLiteral("width"), QString::number(physicalWidth));
    }

    optimized.setQuery(query);
    return optimized;
}

} // namespace Core
} // namespace Acheron

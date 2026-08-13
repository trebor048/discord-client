#include "ImageManager.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QCryptographicHash>
#include <QFile>
#include <QUrlQuery>
#include <QApplication>
#include <QImageReader>

#include "Logging.hpp"

namespace Acheron {
namespace Core {

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
    QNetworkReply *reply = m_nam->get(request);
    m_activeReply = reply;

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

        m_buffer = new QBuffer(this);
        m_buffer->setData(data);
        m_buffer->open(QIODevice::ReadOnly);

        m_movie = new QMovie(this);
        m_movie->setDevice(m_buffer);
        m_movie->setFormat(QByteArrayLiteral("gif"));

        if (!m_movie->isValid() || m_movie->frameCount() == 0) {
            qWarning() << "Invalid GIF data from" << m_url;
            m_movie->deleteLater();
            m_movie = nullptr;
            m_buffer->deleteLater();
            m_buffer = nullptr;
            emit failed();
            return;
        }

        // Determine display size: downscale to container width
        QSize originalSize = m_movie->currentImage().size();
        if (originalSize.isEmpty())
            originalSize = QSize(kGifMaxWidth, kGifMaxWidth);

        if (originalSize.width() > m_containerWidth) {
            int newHeight = originalSize.height() * m_containerWidth / originalSize.width();
            m_displaySize = QSize(m_containerWidth, newHeight);
        } else {
            m_displaySize = originalSize;
        }

        m_movie->setScaledSize(m_displaySize);

        // Pre-cache only the first frame; remaining frames decoded on demand
        ensureFrameCached(0);

        // Connect frame changes
        connect(m_movie, &QMovie::frameChanged, this, &GifAnimation::onMovieFrameChanged);

        emit ready();

        // If we were asked to play before data arrived, start now
        if (m_playing)
            m_movie->start();
    });
}

void GifAnimation::ensureFrameCached(int frameNum) const
{
    if (!m_movie || frameNum < 0)
        return;

    int totalFrames = m_movie->frameCount();
    if (frameNum >= totalFrames)
        return;

    if (m_frameCache.contains(frameNum))
        return;

    // Decode a sliding window of frames around the requested frame
    int windowStart = qMax(0, frameNum - kGifDecodeWindow);
    int windowEnd = qMin(totalFrames - 1, frameNum + kGifDecodeWindow);

    for (int i = windowStart; i <= windowEnd; ++i) {
        if (m_frameCache.contains(i))
            continue;

        m_movie->jumpToFrame(i);
        QImage img = m_movie->currentImage();
        if (!img.isNull())
            m_frameCache.insert(i, QPixmap::fromImage(img));
    }

    // Evict frames far from the current position
    evictDistantFrames(frameNum);

    // Reset to the requested frame position
    m_movie->jumpToFrame(frameNum);
}

void GifAnimation::evictDistantFrames(int currentFrame) const
{
    if (m_frameCache.size() <= kMaxGifFrames)
        return;

    // Evict frames that are outside a generous window
    int evictionThreshold = kMaxGifFrames / 2;
    auto it = m_frameCache.begin();
    while (it != m_frameCache.end()) {
        if (qAbs(it.key() - currentFrame) > evictionThreshold) {
            it = m_frameCache.erase(it);
        } else {
            ++it;
        }
    }
}

QPixmap GifAnimation::currentFrame() const
{
    if (m_movie) {
        int frame = m_movie->currentFrameNumber();

        // Decode on demand if not cached
        if (!m_frameCache.contains(frame))
            ensureFrameCached(frame);

        m_lastAccessedFrame = frame;

        auto it = m_frameCache.constFind(frame);
        if (it != m_frameCache.constEnd())
            return it.value();

        // Fallback: get directly from movie
        QImage img = m_movie->currentImage();
        if (!img.isNull())
            return QPixmap::fromImage(img);
    }
    return QPixmap();
}

int GifAnimation::frameCount() const
{
    if (!m_movie)
        return 0;
    return m_movie->frameCount();
}

void GifAnimation::play()
{
    m_playing = true;
    if (m_movie)
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
    if (!m_movie)
        return;

    QSize originalSize = m_movie->currentImage().size();
    if (originalSize.isEmpty())
        return;

    QSize newSize;
    if (originalSize.width() > m_containerWidth) {
        int newHeight = originalSize.height() * m_containerWidth / originalSize.width();
        newSize = QSize(m_containerWidth, newHeight);
    } else {
        newSize = originalSize;
    }

    if (newSize != m_displaySize) {
        m_displaySize = newSize;
        m_movie->setScaledSize(m_displaySize);
        // Clear cache since scaled size changed — frames will be decoded on demand
        m_frameCache.clear();
    }
}

void GifAnimation::onMovieFrameChanged(int frameNum)
{
    // Decode the frame on demand if not already cached
    ensureFrameCached(frameNum);

    QPixmap frame = currentFrame();
    if (!frame.isNull())
        emit frameChanged(frame);
}

// ---------------------------------------------------------------------------
// ImageManager GIF methods
// ---------------------------------------------------------------------------

GifAnimation *ImageManager::createGifAnimation(const QUrl &url, int containerWidth)
{
    auto it = gifAnimations.constFind(url);
    if (it != gifAnimations.constEnd())
        return it.value();

    auto *anim = new GifAnimation(this);
    anim->load(url, containerWidth);
    gifAnimations.insert(url, anim);

    // Auto-cleanup when the animation is destroyed externally
    connect(anim, &QObject::destroyed, this, [this, url](QObject *) {
        gifAnimations.remove(url);
    });

    return anim;
}

GifAnimation *ImageManager::gifAnimation(const QUrl &url) const
{
    return gifAnimations.value(url, nullptr);
}

void ImageManager::releaseGifAnimation(const QUrl &url)
{
    auto it = gifAnimations.find(url);
    if (it != gifAnimations.end()) {
        it.value()->deleteLater();
        gifAnimations.erase(it);
    }
}

// ---------------------------------------------------------------------------
// ImageManager (existing methods below)
// ---------------------------------------------------------------------------

ImageManager::ImageManager(QObject *parent) : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    cache.setMaxCost(600);

    if (!tempDir.isValid())
        qCWarning(LogCore) << "Failed to create temp directory for image cache";
}

bool ImageManager::isCached(const QUrl &url, const QSize &size)
{
    ImageRequestKey k{ url, size };
    if (pinnedImages.contains(k) || cache.contains(k))
        return true;

    QString path = getCachePath(url, size);
    return QFile::exists(path);
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
        if (pin != PinGroup::None && !pinGroupKeys.contains(pin, k))
            pinGroupKeys.insert(pin, k);
        return pinnedIt.value();
    }

    if (cache.contains(k)) {
        QPixmap pixmap = *cache.object(k);
        if (pin != PinGroup::None) {
            pinnedImages.insert(k, pixmap);
            pinGroupKeys.insert(pin, k);
            cache.remove(k);
        }
        return pixmap;
    }

    // check disk cache
    QString path = getCachePath(url, size);
    if (QFile::exists(path)) {
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
            } else {
                cache.insert(k, new QPixmap(pixmap));
            }
            return pixmap;
        }
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

void ImageManager::request(const QUrl &url, const QSize &size, PinGroup pin)
{
    ImageRequestKey k{ url, size };
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
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, url, size, proxy, dpr]() {
        ImageRequestKey k{ url, size };
        PinGroup pin = pendingPins.value(k, PinGroup::None);
        pendingPins.remove(k);

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(LogCore) << "Failed to fetch image:" << reply->errorString();
            requests.remove(k);
            reply->deleteLater();
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
            QNetworkReply *rawReply = networkManager->get(QNetworkRequest(url));
            connect(rawReply, &QNetworkReply::finished, this,
                    [this, rawReply, url, size, dpr]() {
                ImageRequestKey k{ url, size };
                PinGroup pin = pendingPins.value(k, PinGroup::None);
                pendingPins.remove(k);
                QByteArray rawData = rawReply->readAll();
                rawReply->deleteLater();
                QPixmap raw;
                if (raw.loadFromData(rawData)) {
                    storeFetchedPixmap(k, url, size, rawData, raw, pin, true, dpr);
                } else {
                    qCWarning(LogCore) << "Failed to decode fallback image:" << url;
                    requests.remove(k);
                }
            });
        } else {
            qCWarning(LogCore) << "Failed to decode image:" << url;
            requests.remove(k);
        }
    });
}

void ImageManager::storeFetchedPixmap(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                                      const QByteArray &data, QPixmap pixmap, PinGroup pin, bool proxy,
                                      qreal dpr)
{
    if (proxy) {
        QSize physicalSize(qRound(size.width() * dpr), qRound(size.height() * dpr));
        if (pixmap.size() != physicalSize)
            pixmap = pixmap.scaled(physicalSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        pixmap.setDevicePixelRatio(dpr);
    } else {
        pixmap = pixmap.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Persist to disk only after a successful decode so we never poison the
    // cache with undecodable bytes.
    QFile file(getCachePath(url, size));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }

    if (pin != PinGroup::None) {
        pinnedImages.insert(k, pixmap);
        pinGroupKeys.insert(pin, k);
    } else {
        cache.insert(k, new QPixmap(pixmap));
    }

    requests.remove(k);
    emit imageFetched(url, size, pixmap);
}

void ImageManager::unpinGroup(PinGroup group)
{
    if (group == PinGroup::None)
        return;

    QList<ImageRequestKey> keys = pinGroupKeys.values(group);
    pinGroupKeys.remove(group);

    for (const auto &k : keys) {
        // the same url could be pinned by multiple groups
        bool stillPinned = false;
        for (auto it = pinGroupKeys.cbegin(); it != pinGroupKeys.cend(); ++it) {
            if (it.value() == k) {
                stillPinned = true;
                break;
            }
        }

        if (!stillPinned) {
            auto it = pinnedImages.find(k);
            if (it != pinnedImages.end()) {
                // send it back to the lru
                cache.insert(k, new QPixmap(it.value()));
                pinnedImages.erase(it);
            }
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

    // Request the media proxy's original format instead of forcing webp. Hard
    // coding format=webp broke animated GIFs (and any content the deployed
    // runtime cannot decode as webp). The proxy serves the source format, and
    // fetchFromNetwork() still falls back to the raw proxy URL if decoding
    // fails, so both static images and animated GIFs render.
    // Lossy quality still keeps thumbnails small.
    query.addQueryItem("quality", "80");

    if (displaySize.isValid() && !displaySize.isEmpty()) {
        int physicalWidth = qRound(displaySize.width() * dpr);
        query.addQueryItem("width", QString::number(physicalWidth));
    }

    optimized.setQuery(query);
    return optimized;
}

} // namespace Core
} // namespace Acheron

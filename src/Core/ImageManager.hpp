#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QCache>
#include <QPixmap>
#include <QTemporaryDir>
#include <QHash>
#include <QSet>
#include <QMap>
#include <QBuffer>
#include <QMovie>
#include <QLabel>
#include <QTimer>

#include <limits>
#include <list>
#include <type_traits>

class QNetworkAccessManager;
class QNetworkReply;

namespace Acheron {
namespace Core {

enum class PinGroup {
    None,
    ChannelList,
    ChatView,
};

/// Maximum width in pixels for an animated GIF (container width cap).
static constexpr int kGifMaxWidth = 400;

/// Network transfer timeout for image/GIF downloads.
static constexpr int kTransferTimeoutMs = 30 * 1000;

/// Hard cap on a single download's buffered size; replies are aborted past it.
static constexpr qint64 kMaxDownloadBytes = 50LL * 1024 * 1024;

/// Byte budget for the decoded-pixmap RAM cache (QCache cost = bytes).
static constexpr int kRamCacheBudgetBytes = 256 * 1024 * 1024;

/// Budget for the on-disk session cache; oldest files are pruned past it.
static constexpr qint64 kDiskCacheBudgetBytes = 500LL * 1024 * 1024;
static constexpr int kDiskCacheMaxFiles = 5000;

/// Budgets for loaded GIF animations; least-recently-used ones are unloaded
/// (buffers freed, object kept alive) past these limits.
static constexpr qint64 kGifCacheBudgetBytes = 100LL * 1024 * 1024;
static constexpr int kGifCacheMaxEntries = 200;

/// Approximate decoded byte cost of a pixmap (32bpp).
inline int pixmapCost(const QPixmap &pixmap)
{
    const qint64 bytes = qint64(pixmap.width()) * pixmap.height() * 4;
    return int(qBound<qint64>(1, bytes, qint64(std::numeric_limits<int>::max())));
}

/**
 * Manages playback of a single animated GIF.
 *
 * Wraps QMovie with:
 *   - Automatic downscaling to fit container width (kGifMaxWidth)
 *   - Pause/resume support for hover-to-pause
 *   - Progress callback for network loading
 */
class GifAnimation : public QObject
{
    Q_OBJECT
public:
    explicit GifAnimation(QObject *parent = nullptr);

    /// Load a GIF from the network. Calls @p progress during download
    /// and @p ready when the first frame is available.
    void load(const QUrl &url, int containerWidth = kGifMaxWidth);

    /// Start playback.
    void play();

    /// Pause playback.
    void pause();

    /// Toggle playback state.
    void toggle();

    /// Returns true if currently playing.
    [[nodiscard]] bool isPlaying() const { return m_playing; }

    /// Returns true if the GIF has finished loading from network.
    [[nodiscard]] bool isReady() const { return m_movie != nullptr || !m_staticImage.isNull(); }

    /// Returns true if the GIF is still downloading.
    [[nodiscard]] bool isLoading() const { return m_loading; }

    /// Returns the current frame as a QPixmap.
    [[nodiscard]] QPixmap currentFrame() const;

    /// Returns the number of frames.
    [[nodiscard]] int frameCount() const;

    /// Set the container width for downscaling.
    void setContainerWidth(int width);

    /// Returns the display size of the (scaled) GIF.
    [[nodiscard]] QSize displaySize() const { return m_displaySize; }

    /// Returns a progress percentage (0-100) for network loading, or -1 if not loading.
    [[nodiscard]] int loadProgress() const { return m_loadProgress; }

    /// Approximate memory held by this animation (raw buffer + decoded frames).
    [[nodiscard]] qint64 memoryCost() const;

    /// Frees the raw buffer, movie and decoded frames but keeps the object
    /// alive so external raw pointers stay valid; a later play() reloads.
    void unload();

signals:
    /// Emitted when the first frame is available (load finished).
    void ready();

    /// Emitted when a new frame is available for rendering.
    void frameChanged(const QPixmap &frame);

    /// Emitted during network download.
    void loadProgressChanged(int percent);

    /// Emitted if the GIF failed to load or decode, so callers can release and retry.
    void failed();

private:
    void onMovieFrameChanged(int frameNum);

    // Computes the downscaled display size for a source of the given size.
    QSize computeDisplaySize(const QSize &originalSize) const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QBuffer *m_buffer = nullptr;
    QMovie *m_movie = nullptr;
    // Static fallback for payloads that are not animations QMovie understands
    // (e.g. a static webp/png poster served for a gifv embed thumbnail).
    QImage m_staticImage;
    mutable QPixmap m_staticScaled;
    bool m_playing = false;
    bool m_loading = false;
    int m_loadProgress = -1;
    int m_containerWidth = kGifMaxWidth;
    QSize m_displaySize;
    QUrl m_url;
};

inline size_t qHash(PinGroup key, size_t seed = 0) noexcept
{
    return ::qHash(static_cast<std::underlying_type_t<PinGroup>>(key), seed);
}

struct ImageRequestKey
{
    QUrl url;
    QSize size;

    bool operator==(const ImageRequestKey &other) const
    {
        return url == other.url && size == other.size;
    }
};

inline size_t qHash(const ImageRequestKey &key, size_t seed = 0) noexcept
{
    return qHashMulti(seed, key.url, key.size);
}

class ImageManager : public QObject
{
    Q_OBJECT
public:
    explicit ImageManager(QObject *parent = nullptr);

    static constexpr int MaxDisplayWidth = 400;
    static constexpr int MaxDisplayHeight = 300;

    [[nodiscard]] bool isCached(const QUrl &url, const QSize &size);
    void assign(QLabel *label, const QUrl &url, const QSize &size);
    QPixmap get(const QUrl &url, const QSize &size, PinGroup pin = PinGroup::None);
    QPixmap getIfCached(const QUrl &url, const QSize &size, PinGroup pin = PinGroup::None);
    [[nodiscard]] QPixmap placeholder(const QSize &size);

    void unpinGroup(PinGroup group);

    /// Creates a GifAnimation for the given URL.
    /// The ImageManager retains ownership. Returns nullptr if the URL is not a GIF.
    /// Set @p containerWidth to the chat viewport width for downscaling.
    GifAnimation *createGifAnimation(const QUrl &url, int containerWidth = kGifMaxWidth);

    /// Returns an existing GifAnimation for the given URL, or nullptr.
    GifAnimation *gifAnimation(const QUrl &url) const;

    /// Removes a GifAnimation (e.g. when the message scrolls out of view).
    void releaseGifAnimation(const QUrl &url);

    [[nodiscard]] static QSize calculateDisplaySize(const QSize &original);

    /// Attaches download-size guards (Content-Length + accumulated bytes) to
    /// a reply, aborting it past kMaxDownloadBytes. Also used by GifAnimation.
    static void guardReply(QNetworkReply *reply);

    /// Returns true if the last fetch for this url+size failed (network or
    /// decode). Failed requests are not re-issued until clearFailedRequests().
    [[nodiscard]] bool hasFailed(const QUrl &url, const QSize &size) const;

    /// Clears the failure records so images can be fetched again (e.g. on
    /// channel switch).
    void clearFailedRequests();

signals:
    void imageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

    /// Emitted when a fetch definitively fails (after any fallback), so views
    /// can repaint an error state instead of an eternal placeholder.
    void imageFailed(const QUrl &url, const QSize &size);

private:
    QPixmap getImpl(const QUrl &url, const QSize &size, PinGroup pin, bool fetchIfNeeded);
    void request(const QUrl &url, const QSize &size, PinGroup pin);
    void fetchFromNetwork(const QUrl &url, const QSize &size, PinGroup pin);
    void fetchRawProxyUrl(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                          qreal dpr);
    void failRequest(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                     const QString &reason);
    void storeFetchedPixmap(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                            const QByteArray &data, QPixmap pixmap, PinGroup pin, bool proxy,
                            qreal dpr);
    void onDiskCacheWritten(const ImageRequestKey &k, const QString &path, bool ok);
    QString getCachePath(const QUrl &url, const QSize &size) const;
    static bool isDiscordProxyUrl(const QUrl &url);
    static QUrl buildOptimizedUrl(const QUrl &proxyUrl, const QSize &displaySize, qreal dpr);
    void pruneDiskCache();
    void touchGifAnimation(const QUrl &url) const;
    void enforceGifCacheLimits(const QUrl &mostRecent);
    void recalcGifMemoryTotal();

    QNetworkAccessManager *networkManager;
    QTemporaryDir tempDir;
    int storesSincePrune = 0;

    QSet<ImageRequestKey> requests;
    QSet<ImageRequestKey> failedRequests;
    QHash<ImageRequestKey, PinGroup> pendingPins;
    QCache<ImageRequestKey, QPixmap> cache;
    QHash<ImageRequestKey, QPixmap> pinnedImages;
    QMultiHash<PinGroup, ImageRequestKey> pinGroupKeys;
    QHash<ImageRequestKey, int> pinRefCounts;
    QSet<ImageRequestKey> diskCacheKeys;
    QHash<QString, ImageRequestKey> diskCachePathToKey;
    QHash<QUrl, GifAnimation *> gifAnimations;
    mutable std::list<QUrl> gifLruOrder; // front = least recently used
    mutable QHash<QUrl, std::list<QUrl>::iterator> gifLruMap;
    qint64 gifMemoryTotal = 0;
};

} // namespace Core
} // namespace Acheron

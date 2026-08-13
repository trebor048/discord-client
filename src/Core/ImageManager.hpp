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

/// Maximum number of decoded frames cached per animated GIF.
/// Frames are decoded on-demand in a sliding window around the current frame.
static constexpr int kMaxGifFrames = 30;

/// Window size for pre-decoding frames ahead of the current position.
static constexpr int kGifDecodeWindow = 5;

/// Maximum width in pixels for an animated GIF (container width cap).
static constexpr int kGifMaxWidth = 400;

/**
 * Manages playback of a single animated GIF.
 *
 * Wraps QMovie with:
 *   - Frame cache to avoid re-decoding on scroll
 *   - Hard frame limit (kMaxGifFrames) to prevent memory bombs
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
    [[nodiscard]] bool isReady() const { return m_movie != nullptr; }

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
    void ensureFrameCached(int frameNum) const;
    void evictDistantFrames(int currentFrame) const;

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QBuffer *m_buffer = nullptr;
    QMovie *m_movie = nullptr;
    mutable QMap<int, QPixmap> m_frameCache;
    mutable int m_lastAccessedFrame = 0;
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

signals:
    void imageFetched(const QUrl &url, const QSize &size, const QPixmap &pixmap);

private:
    QPixmap getImpl(const QUrl &url, const QSize &size, PinGroup pin, bool fetchIfNeeded);
    void request(const QUrl &url, const QSize &size, PinGroup pin);
    void fetchFromNetwork(const QUrl &url, const QSize &size, PinGroup pin);
    void storeFetchedPixmap(const ImageRequestKey &k, const QUrl &url, const QSize &size,
                            const QByteArray &data, QPixmap pixmap, PinGroup pin, bool proxy,
                            qreal dpr);
    QString getCachePath(const QUrl &url, const QSize &size) const;
    static bool isDiscordProxyUrl(const QUrl &url);
    static QUrl buildOptimizedUrl(const QUrl &proxyUrl, const QSize &displaySize, qreal dpr);

    QNetworkAccessManager *networkManager;
    QTemporaryDir tempDir;

    QSet<ImageRequestKey> requests;
    QHash<ImageRequestKey, PinGroup> pendingPins;
    QCache<ImageRequestKey, QPixmap> cache;
    QHash<ImageRequestKey, QPixmap> pinnedImages;
    QMultiHash<PinGroup, ImageRequestKey> pinGroupKeys;
    QHash<QUrl, GifAnimation *> gifAnimations;
};

} // namespace Core
} // namespace Acheron

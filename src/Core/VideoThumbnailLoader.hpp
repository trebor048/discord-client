#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QMediaPlayer;
class QTimer;
class QVideoFrame;
class QVideoSink;
QT_END_NAMESPACE

namespace Acheron {
namespace Core {

/// Extracts a representative thumbnail frame from a video URL by seeking a
/// short way into the clip, grabbing one frame, and stopping. A single
/// QMediaPlayer instance is shared and requests are processed one at a time
/// off a FIFO queue, so thumbnails trickle in without spinning up a player
/// per attachment. Frames are cached in memory for the session.
class VideoThumbnailLoader : public QObject
{
    Q_OBJECT
public:
    explicit VideoThumbnailLoader(QObject *parent = nullptr);
    ~VideoThumbnailLoader() override;

    /// Enqueue a video URL for thumbnail extraction. Requests already cached,
    /// in flight, queued, or known-failed are ignored.
    void request(const QUrl &url);

    /// The cached frame for a URL, or a null pixmap when not yet available.
    [[nodiscard]] QPixmap cached(const QUrl &url) const;
    /// True when a URL definitively failed to produce a frame (so callers
    /// don't re-request it on every repaint).
    [[nodiscard]] bool isFailed(const QUrl &url) const;

signals:
    void thumbnailReady(const QUrl &url, const QPixmap &pixmap);

private:
    void ensurePlayer();
    void processNext();
    void grabFrame(const QVideoFrame &frame);
    void failCurrent();

    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QTimer *m_watchdog = nullptr;

    QList<QUrl> m_queue;
    QSet<QUrl> m_inFlight;
    QSet<QUrl> m_failed;
    QHash<QUrl, QPixmap> m_cache;
    QUrl m_current;
    qint64 m_grabAtMs = 0;

    static constexpr int kMaxQueue = 16;
    static constexpr int kMaxCache = 128;
    static constexpr int kMaxFailed = 256;
    static constexpr qint64 kWatchdogMs = 12000;
};

} // namespace Core
} // namespace Acheron

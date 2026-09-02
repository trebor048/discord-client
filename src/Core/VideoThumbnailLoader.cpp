#include "VideoThumbnailLoader.hpp"

#include "NetUtils.hpp"

#include <QHostAddress>
#include <QImage>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>

namespace Acheron {
namespace Core {

VideoThumbnailLoader::VideoThumbnailLoader(QObject *parent)
    : QObject(parent)
{
}

VideoThumbnailLoader::~VideoThumbnailLoader() = default;

void VideoThumbnailLoader::ensurePlayer()
{
    if (m_player)
        return;

    m_player = new QMediaPlayer(this);
    m_sink = new QVideoSink(this);
    m_player->setVideoOutput(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged, this,
            [this](const QVideoFrame &frame) {
                if (!m_current.isValid())
                    return;
                // m_grabAtMs < 0 means the current source hasn't produced a
                // duration or content status yet — ignore any frame, including
                // late/asynchronous frames from the PREVIOUS media's pipeline
                // that arrive during teardown (they would otherwise be grabbed
                // and cached under the new URL).
                if (m_grabAtMs < 0)
                    return;
                // Only grab once we're past the seek target so the frame isn't
                // a black opening frame; if the duration never arrives
                // (streaming), m_grabAtMs is set to 0 by the content status and
                // the first frame is used.
                if (m_player->position() < m_grabAtMs)
                    return;
                // Frames are flowing — a slow-but-alive load must not trip the
                // watchdog (which would permanently mark the URL failed).
                m_watchdog->start();
                grabFrame(frame);
            });

    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (!m_current.isValid() || duration <= 0)
            return;
        // Seek a short way into the clip for a representative frame.
        m_grabAtMs = qMin<qint64>(duration / 4, 1000);
        m_watchdog->start();
        if (m_grabAtMs > 0)
            m_player->setPosition(m_grabAtMs);
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::InvalidMedia) {
                    failCurrent();
                    return;
                }
                // The source has real content (buffering/loaded/end): arm the
                // frame grab for sources that never deliver a duration
                // (streaming), where the first frame should be used.
                if (m_current.isValid() && m_grabAtMs < 0
                    && (status == QMediaPlayer::LoadedMedia
                        || status == QMediaPlayer::BufferedMedia
                        || status == QMediaPlayer::EndOfMedia)) {
                    m_grabAtMs = 0;
                    m_watchdog->start();
                }
            });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &) {
                if (error != QMediaPlayer::NoError)
                    failCurrent();
            });

    m_watchdog = new QTimer(this);
    m_watchdog->setSingleShot(true);
    m_watchdog->setInterval(kWatchdogMs);
    connect(m_watchdog, &QTimer::timeout, this, [this]() { failCurrent(); });
}

void VideoThumbnailLoader::request(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty())
        return;
    if (NetUtils::isPrivateHost(url.host())) {
        if (m_failed.size() >= kMaxFailed) m_failed.erase(m_failed.begin());
        m_failed.insert(url);
        return;
    }
    if (m_cache.contains(url) || m_failed.contains(url) || m_inFlight.contains(url) ||
        m_queue.contains(url))
        return;
    if (m_queue.size() >= kMaxQueue)
        return;
    m_queue.append(url);
    processNext();
}

QPixmap VideoThumbnailLoader::cached(const QUrl &url) const
{
    return m_cache.value(url);
}

bool VideoThumbnailLoader::isFailed(const QUrl &url) const
{
    return m_failed.contains(url);
}

void VideoThumbnailLoader::processNext()
{
    if (m_current.isValid() || m_queue.isEmpty())
        return;

    m_current = m_queue.takeFirst();
    m_inFlight.insert(m_current);
    // Not armed yet: frames arriving before the new source produces a
    // duration/content status belong to the previous media and must be ignored
    // (see the videoFrameChanged guard).
    m_grabAtMs = -1;

    ensurePlayer();
    m_player->stop();
    m_player->setSource(QUrl());
    m_player->setSource(m_current);
    m_player->play();
    m_watchdog->start();
}

void VideoThumbnailLoader::grabFrame(const QVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    // toImage() handles all pixel formats (RGB and YUV/NV12 hardware frames)
    // and produces a detached copy, so the result outlives the mapped buffer.
    const QImage image = frame.toImage();
    if (image.isNull()) {
        failCurrent();
        return;
    }

    // Cache a bounded preview (aspect preserved).
    QSize display = image.size();
    display.scale(480, 480, Qt::KeepAspectRatio);
    QPixmap pixmap = QPixmap::fromImage(
            display == image.size() ? image
                                    : image.scaled(display, Qt::KeepAspectRatio,
                                                   Qt::SmoothTransformation));
    if (pixmap.isNull()) {
        failCurrent();
        return;
    }

    if (m_cache.size() >= kMaxCache)
        m_cache.erase(m_cache.begin());
    m_cache.insert(m_current, pixmap);

    m_watchdog->stop();
    const QUrl done = m_current;
    m_current = QUrl();
    m_inFlight.remove(done);
    m_player->stop();
    m_player->setSource(QUrl());

    emit thumbnailReady(done, pixmap);
    processNext();
}

void VideoThumbnailLoader::failCurrent()
{
    m_watchdog->stop();
    if (m_current.isValid()) {
        if (m_failed.size() >= kMaxFailed)
            m_failed.erase(m_failed.begin());
        m_failed.insert(m_current);
        m_inFlight.remove(m_current);
        m_current = QUrl();
    }
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    processNext();
}

} // namespace Core
} // namespace Acheron

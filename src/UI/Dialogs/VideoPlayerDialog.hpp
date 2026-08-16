#pragma once

#include <QDialog>
#include <QMediaPlayer>
#include <QUrl>

class QAudioOutput;
class QVideoWidget;
class QPushButton;
class QSlider;
class QLabel;
class QKeyEvent;

namespace Acheron {
namespace UI {

/// A lightweight, in-app video player used when the user clicks a video
/// attachment or a video embed in chat. Owns a QMediaPlayer + QAudioOutput +
/// QVideoWidget and exposes transport controls (play/pause, seek, volume).
/// Styled to match the app's dark theme.
class VideoPlayerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit VideoPlayerDialog(const QUrl &source, QWidget *parent = nullptr);

private slots:
    void togglePlayPause();
    void seekSliderPressed();
    void seekSliderReleased();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void seekRelative(qint64 delta);
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void updateSlider(qint64 position);
    void updatePlayPauseIcon();
    static QString formatTime(qint64 ms);

    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    QVideoWidget *videoWidget;
    QPushButton *playButton;
    QSlider *positionSlider;
    QLabel *timeLabel;
    QSlider *volumeSlider;
    QLabel *statusLabel;
    bool sliderDragging = false;
};

} // namespace UI
} // namespace Acheron

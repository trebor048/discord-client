#include "VideoPlayerDialog.hpp"

#include <QAudioOutput>
#include <QVideoWidget>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace Acheron {
namespace UI {

VideoPlayerDialog::VideoPlayerDialog(const QUrl &source, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Video Player"));
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    resize(720, 480);
    setAttribute(Qt::WA_DeleteOnClose);

    // Dark theme matching the app. Uses the same muted grays as the chat
    // placeholder and a Discord-like accent for the seek bar fill.
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1e1e1e; }"
        "QVideoWidget { background: #000000; }"
        "QSlider::groove:horizontal { height: 4px; background: #3c3c3c; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #5865f2; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 12px; margin: -4px 0; border-radius: 6px; background: #ffffff; }"
        "QPushButton { background: #3c3c3c; color: #ffffff; border: none; border-radius: 4px; padding: 6px 12px; }"
        "QPushButton:hover { background: #4c4c4c; }"
        "QLabel { color: #dcdcdc; background: transparent; }"
    ));

    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    audioOutput->setVolume(0.8);
    player->setAudioOutput(audioOutput);

    videoWidget = new QVideoWidget(this);
    player->setVideoOutput(videoWidget);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);
    root->addWidget(videoWidget, 1);

    statusLabel = new QLabel(tr("Loading…"), this);
    statusLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(statusLabel);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);

    playButton = new QPushButton(QStringLiteral("▶"), this);
    playButton->setFixedSize(40, 32);
    controls->addWidget(playButton);

    positionSlider = new QSlider(Qt::Horizontal, this);
    positionSlider->setRange(0, 0);
    controls->addWidget(positionSlider, 1);

    timeLabel = new QLabel(QStringLiteral("0:00 / 0:00"), this);
    controls->addWidget(timeLabel);

    volumeSlider = new QSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(80);
    volumeSlider->setFixedWidth(90);
    controls->addWidget(volumeSlider);

    root->addLayout(controls);

    connect(playButton, &QPushButton::clicked, this, &VideoPlayerDialog::togglePlayPause);
    connect(positionSlider, &QSlider::sliderPressed, this,
            &VideoPlayerDialog::seekSliderPressed);
    connect(positionSlider, &QSlider::sliderReleased, this,
            &VideoPlayerDialog::seekSliderReleased);
    connect(positionSlider, &QSlider::sliderMoved, this, [this](int pos) {
        timeLabel->setText(formatTime(pos) + QStringLiteral(" / ") +
                           formatTime(player->duration()));
    });
    connect(volumeSlider, &QSlider::valueChanged, this,
            [this](int value) { audioOutput->setVolume(qreal(value) / 100.0); });

    connect(player, &QMediaPlayer::positionChanged, this,
            &VideoPlayerDialog::onPositionChanged);
    connect(player, &QMediaPlayer::durationChanged, this,
            &VideoPlayerDialog::onDurationChanged);
    connect(player, &QMediaPlayer::playbackStateChanged, this,
            &VideoPlayerDialog::onPlaybackStateChanged);
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            &VideoPlayerDialog::onMediaStatusChanged);
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error error, const QString &errorString) {
                if (error != QMediaPlayer::NoError)
                    statusLabel->setText(tr("Playback error: %1").arg(errorString));
            });

    player->setSource(source);
    player->play();
}

void VideoPlayerDialog::togglePlayPause()
{
    if (player->playbackState() == QMediaPlayer::PlayingState)
        player->pause();
    else
        player->play();
}

void VideoPlayerDialog::seekSliderPressed()
{
    sliderDragging = true;
}

void VideoPlayerDialog::seekSliderReleased()
{
    sliderDragging = false;
    player->setPosition(positionSlider->value());
}

void VideoPlayerDialog::onPositionChanged(qint64 position)
{
    if (sliderDragging)
        return;
    updateSlider(position);
}

void VideoPlayerDialog::onDurationChanged(qint64 duration)
{
    positionSlider->setRange(0, int(duration));
    updateSlider(player->position());
}

void VideoPlayerDialog::onPlaybackStateChanged(QMediaPlayer::PlaybackState)
{
    updatePlayPauseIcon();
}

void VideoPlayerDialog::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::BufferingMedia:
    case QMediaPlayer::LoadingMedia:
        statusLabel->setText(tr("Loading…"));
        break;
    case QMediaPlayer::EndOfMedia:
        statusLabel->setText(tr("End of video"));
        break;
    case QMediaPlayer::InvalidMedia:
        statusLabel->setText(tr("Cannot play this video"));
        break;
    default:
        statusLabel->clear();
        break;
    }
}

void VideoPlayerDialog::updateSlider(qint64 position)
{
    // Guard against feedback loops: while the user drags, or while we program
    // the value, ignore valueChanged so the player is not seeked unintentionally.
    if (positionSlider->isSliderDown())
        return;

    positionSlider->blockSignals(true);
    positionSlider->setValue(int(position));
    positionSlider->blockSignals(false);

    timeLabel->setText(formatTime(position) + QStringLiteral(" / ") +
                       formatTime(player->duration()));
}

void VideoPlayerDialog::updatePlayPauseIcon()
{
    const bool playing = player->playbackState() == QMediaPlayer::PlayingState;
    playButton->setText(playing ? QStringLiteral("❚❚") : QStringLiteral("▶"));
}

QString VideoPlayerDialog::formatTime(qint64 ms)
{
    const qint64 totalSecs = ms / 1000;
    const qint64 mins = totalSecs / 60;
    const qint64 secs = totalSecs % 60;
    return QString("%1:%2").arg(mins).arg(secs, 2, 10, QLatin1Char('0'));
}

} // namespace UI
} // namespace Acheron

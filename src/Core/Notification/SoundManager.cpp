#include "SoundManager.hpp"

#include <QStandardPaths>
#include <QFile>
#include <QDebug>
#include <QDir>
#include <QAudioFormat>
#include <QAudioBuffer>
#include <cmath>

namespace Acheron {
namespace Core {

SoundManager::SoundManager(QObject *parent)
    : QObject(parent)
{
}

SoundManager::~SoundManager()
{
    shutdown();
}

void SoundManager::initialize()
{
    if (m_initialized) return;

    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(m_globalVolume / 100.0);

    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &errorString) {
        Q_UNUSED(error);
        emit errorOccurred(errorString);
    });

    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            m_player->setSource(QUrl());
        }
    });

    // Pre-generate all built-in sounds
    generateBuiltinSounds();

    m_initialized = true;
}

void SoundManager::shutdown()
{
    if (m_player) {
        m_player->stop();
        m_player->deleteLater();
        m_player = nullptr;
    }
    if (m_audioOutput) {
        m_audioOutput->deleteLater();
        m_audioOutput = nullptr;
    }
    m_soundBuffers.clear();
    m_initialized = false;
}

void SoundManager::generateBuiltinSounds()
{
    // Generate simple notification tones using sine waves
    struct SoundDef {
        const char* id;
        double frequency;  // Hz
        double duration;   // seconds
        int volume;        // 0-100
    };

    const SoundDef sounds[] = {
        {DefaultNotification, 880.0, 0.15, 80},    // A5 - short beep
        {Message1, 660.0, 0.2, 70},                 // E5 - message tone
        {Message2, 784.0, 0.2, 70},                 // G5 - reply tone
        {Message3, 523.0, 0.25, 65},                // C5 - DM tone
        {Mention1, 1046.0, 0.1, 85},                // C6 - mention
        {Mention2, 1318.0, 0.1, 90},                // E6 - @everyone
        {Mention3, 1174.0, 0.1, 85},                // D6 - @here
    };

    for (const auto& def : sounds) {
        generateTone(def.id, def.frequency, def.duration, def.volume);
    }
}

void SoundManager::generateTone(const QString& soundId, double frequency, double duration, int volume)
{
    const int sampleRate = 44100;
    const int numSamples = static_cast<int>(sampleRate * duration);
    
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Float);

    QByteArray sampleData(numSamples * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
    QAudioBuffer buffer(sampleData, format);
    float *data = buffer.data<float>();
    
    const float amplitude = volume / 100.0f * 0.3f;  // Scale to prevent clipping
    const double phaseIncrement = 2.0 * M_PI * frequency / sampleRate;
    double phase = 0.0;

    for (int i = 0; i < numSamples; ++i) {
        float sample = amplitude * std::sin(phase);
        // Apply envelope (fade in/out to avoid clicks)
        float envelope = 1.0f;
        if (i < sampleRate * 0.01) envelope = i / (sampleRate * 0.01f);  // 10ms fade in
        else if (i > numSamples - sampleRate * 0.01) envelope = (numSamples - i) / (sampleRate * 0.01f);  // 10ms fade out
        
        data[i * 2] = sample * envelope;     // Left channel
        data[i * 2 + 1] = sample * envelope; // Right channel
        phase += phaseIncrement;
    }

    // Store as WAV data in memory
    QByteArray wavData = audioBufferToWav(buffer);
    m_soundBuffers[soundId] = wavData;
}

QByteArray SoundManager::audioBufferToWav(const QAudioBuffer& buffer)
{
    QByteArray wavData;
    QDataStream stream(&wavData, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    const int sampleRate = buffer.format().sampleRate();
    const int channels = buffer.format().channelCount() > 0 ? buffer.format().channelCount() : 1;
    const int bitsPerSample = 16;
    const int byteRate = sampleRate * channels * bitsPerSample / 8;
    const int blockAlign = channels * bitsPerSample / 8;
    // buffer.byteCount() is the interleaved float size (frames*channels*4); we
    // write 16-bit PCM (frames*channels*2), so the data chunk must match the
    // actual bytes written or players truncate/misread the file.
    const int dataSize = static_cast<int>(buffer.frameCount()) * channels * bitsPerSample / 8;
    const int fileSize = 36 + dataSize;

    // RIFF header
    stream.writeRawData("RIFF", 4);
    stream << static_cast<quint32>(fileSize);
    stream.writeRawData("WAVE", 4);

    // fmt chunk
    stream.writeRawData("fmt ", 4);
    stream << static_cast<quint32>(16);  // PCM format chunk size
    stream << static_cast<quint16>(1);   // PCM
    stream << static_cast<quint16>(channels);
    stream << static_cast<quint32>(sampleRate);
    stream << static_cast<quint32>(byteRate);
    stream << static_cast<quint16>(blockAlign);
    stream << static_cast<quint16>(bitsPerSample);

    // data chunk
    stream.writeRawData("data", 4);
    stream << static_cast<quint32>(dataSize);

    // Convert float samples to 16-bit PCM
    const float *floatData = buffer.constData<float>();
    const int numSamples = static_cast<int>(buffer.frameCount()) * channels;
    for (int i = 0; i < numSamples; ++i) {
        qint16 sample = static_cast<qint16>(qBound(-32768.0, floatData[i] * 32767.0, 32767.0));
        stream << sample;
    }

    return wavData;
}

void SoundManager::playNotificationSound(const QString &soundId, int volume)
{
    if (!m_initialized || m_globalVolume == 0 || volume == 0)
        return;

    ensurePlayer();

    auto it = m_soundBuffers.find(soundId);
    if (it == m_soundBuffers.end()) {
        // Try to load from resources as fallback
        QUrl url = getSoundUrl(soundId);
        if (url.isValid()) {
            m_player->setSource(url);
            m_audioOutput->setVolume((m_globalVolume * volume) / 10000.0);
            m_player->play();
            emit soundPlayed(soundId);
            return;
        }
        qWarning() << "Sound not found:" << soundId;
        return;
    }

    // Play from memory buffer
    // Stop current playback first so the output device doesn't click/pop
    // during the source swap (fresh buffers can emit a short artifact first).
    m_player->stop();

    if (m_currentBuffer) {
        m_currentBuffer->deleteLater();
        m_currentBuffer = nullptr;
    }
    QBuffer* audioBuffer = new QBuffer(this);
    m_currentBuffer = audioBuffer;
    audioBuffer->setData(it.value());
    audioBuffer->open(QIODevice::ReadOnly);

    m_player->setSourceDevice(audioBuffer, QUrl("data:audio/wav"));
    m_audioOutput->setVolume((m_globalVolume * volume) / 10000.0);
    m_player->play();

    connect(m_player, &QMediaPlayer::mediaStatusChanged, audioBuffer, [this, audioBuffer](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
            if (m_currentBuffer == audioBuffer)
                m_currentBuffer = nullptr;
            audioBuffer->deleteLater();
        }
    });

    emit soundPlayed(soundId);
}

void SoundManager::playUrl(const QUrl &url, int volume)
{
    if (!m_initialized || m_globalVolume == 0 || volume == 0 || !url.isValid())
        return;

    ensurePlayer();

    if (m_currentBuffer) {
        m_currentBuffer->deleteLater();
        m_currentBuffer = nullptr;
    }

    m_player->setSource(url);
    m_audioOutput->setVolume((m_globalVolume * volume) / 10000.0);
    m_player->play();
}

void SoundManager::playCustomSound(const QByteArray &audioData, const QString &format, int volume)
{
    if (!m_initialized || m_globalVolume == 0 || volume == 0 || audioData.isEmpty())
        return;

    ensurePlayer();
    playFromBuffer(audioData, format, volume);
}

void SoundManager::setGlobalVolume(int volume)
{
    m_globalVolume = qBound(0, volume, 100);
    if (m_audioOutput) {
        m_audioOutput->setVolume(m_globalVolume / 100.0);
    }
}

int SoundManager::globalVolume() const
{
    return m_globalVolume;
}

void SoundManager::cacheSound(const QString &soundId, const QByteArray &data, const QString &format)
{
    QMutexLocker locker(&m_cacheMutex);
    m_soundCache[soundId] = {data, format};
}

bool SoundManager::hasCachedSound(const QString &soundId) const
{
    QMutexLocker locker(&m_cacheMutex);
    return m_soundCache.contains(soundId);
}

void SoundManager::playCachedSound(const QString &soundId, int volume)
{
    if (!m_initialized || m_globalVolume == 0 || volume == 0)
        return;

    CachedSound cached;
    {
        QMutexLocker locker(&m_cacheMutex);
        auto it = m_soundCache.constFind(soundId);
        if (it == m_soundCache.constEnd())
            return;
        cached = it.value();
    }

    ensurePlayer();
    playFromBuffer(cached.data, cached.format, volume);
    emit soundPlayed(soundId);
}

void SoundManager::clearCache()
{
    QMutexLocker locker(&m_cacheMutex);
    m_soundCache.clear();
}

void SoundManager::ensurePlayer()
{
    if (!m_player) {
        initialize();
    }
}

QUrl SoundManager::getSoundUrl(const QString &soundId) const
{
    // Check cache first
    {
        QMutexLocker locker(&m_cacheMutex);
        if (m_soundCache.contains(soundId)) {
            return QUrl(); // Will be handled by playFromBuffer
        }
    }

    QString fileName;
    if (soundId == DefaultNotification) fileName = ":/sounds/notification.mp3";
    else if (soundId == Message1) fileName = ":/sounds/message1.mp3";
    else if (soundId == Message2) fileName = ":/sounds/message2.mp3";
    else if (soundId == Message3) fileName = ":/sounds/message3.mp3";
    else if (soundId == Mention1) fileName = ":/sounds/mention1.wav";
    else if (soundId == Mention2) fileName = ":/sounds/mention2.wav";
    else if (soundId == Mention3) fileName = ":/sounds/mention3.mp3";
    else return QUrl();

    return QUrl(fileName);
}

void SoundManager::playFromBuffer(const QByteArray &data, const QString &format, int volume)
{
    if (!m_initialized || m_globalVolume == 0 || volume == 0 || data.isEmpty())
        return;

    ensurePlayer();

    // Stop any current playback first so the output device doesn't click/pop
    // during the source swap (a fresh buffer can otherwise emit a short
    // artifact right before the new sound starts).
    m_player->stop();

    if (m_currentBuffer) {
        m_currentBuffer->deleteLater();
        m_currentBuffer = nullptr;
    }
    QBuffer* buffer = new QBuffer(this);
    m_currentBuffer = buffer;
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);

    m_player->setSourceDevice(buffer, QUrl(QString("data:audio/%1").arg(format)));
    m_audioOutput->setVolume((m_globalVolume * volume) / 10000.0);
    m_player->play();

    connect(m_player, &QMediaPlayer::mediaStatusChanged, buffer, [this, buffer](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia || status == QMediaPlayer::InvalidMedia) {
            if (m_currentBuffer == buffer)
                m_currentBuffer = nullptr;
            buffer->deleteLater();
        }
    });
}

} // namespace Core
} // namespace Acheron

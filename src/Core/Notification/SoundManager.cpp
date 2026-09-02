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
        // Log here too — the errorOccurred signal had no receivers, so playback
        // errors were silently dropped.
        qWarning() << "Sound playback error:" << static_cast<int>(error) << errorString;
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
    // Pleasant two-tone chimes instead of harsh short beeps: each chime is a
    // pair of bell-like notes with a soft quarter-sine attack and an
    // exponential decay, so nothing sounds like a click. Frequency pairs are
    // chosen as pleasant intervals (fifths/octaves).
    struct ChimeDef {
        const char *id;
        double masterVolume; // 0..1 overall gain
        QList<ChimeNote> notes;
    };

    const QList<ChimeDef> chimes = {
        // Default: soft single "ding"
        { DefaultNotification, 0.7,
          { { 880.0, 0.00, 0.50, 1.0 }, { 1760.0, 0.00, 0.28, 0.35 } } },
        // Message tones: gentle ascending fifths
        { Message1, 0.7,
          { { 523.25, 0.00, 0.45, 1.0 }, { 783.99, 0.10, 0.40, 0.7 } } },   // C5 -> G5
        { Message2, 0.7,
          { { 587.33, 0.00, 0.45, 1.0 }, { 880.00, 0.10, 0.40, 0.7 } } },   // D5 -> A5
        { Message3, 0.72,
          { { 659.25, 0.00, 0.45, 1.0 }, { 987.77, 0.10, 0.40, 0.7 } } },   // E5 -> B5
        // Mentions: brighter and a touch louder
        { Mention1, 0.78,
          { { 783.99, 0.00, 0.45, 1.0 }, { 1174.66, 0.08, 0.42, 0.75 } } }, // G5 -> D6
        { Mention2, 0.82,
          { { 880.00, 0.00, 0.48, 1.0 }, { 1318.51, 0.08, 0.45, 0.8 } } },  // A5 -> E6
        { Mention3, 0.78,
          { { 830.61, 0.00, 0.45, 1.0 }, { 1244.51, 0.08, 0.42, 0.75 } } }, // G#5 -> D#6
    };

    for (const auto &def : chimes)
        generateChime(def.id, def.notes, def.masterVolume);
}

void SoundManager::generateChime(const QString &soundId, const QList<ChimeNote> &notes,
                                 double masterVolume)
{
    const int sampleRate = 44100;
    const double attackTime = 0.012; // 12 ms soft quarter-sine attack
    const double releaseTime = 0.05; // 50 ms tail fade to zero

    // Total duration covers the longest note plus a release tail.
    double totalDuration = 0.0;
    for (const auto &note : notes)
        totalDuration = std::max(totalDuration, note.start + note.duration);
    totalDuration += releaseTime;

    const int numSamples = static_cast<int>(sampleRate * totalDuration) + 1;

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::SampleFormat::Float);

    QByteArray sampleData(numSamples * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
    QAudioBuffer buffer(sampleData, format);
    float *data = buffer.data<float>();
    std::memset(data, 0, static_cast<size_t>(numSamples) * 2 * sizeof(float));

    const double overallGain = masterVolume * 0.22;

    for (const auto &note : notes) {
        const int startSample = static_cast<int>(note.start * sampleRate);
        const int noteSamples = static_cast<int>(note.duration * sampleRate);
        const int endSample = std::min(numSamples, startSample + noteSamples);
        // Exponential decay so the partial rings out instead of cutting off.
        // Target ~ -40 dB at the note's nominal end.
        const double decayPerSec = std::log(100.0) / note.duration;
        const double phaseIncrement = 2.0 * M_PI * note.frequency / sampleRate;

        for (int i = startSample; i < endSample; ++i) {
            const int t = i - startSample;
            const double time = static_cast<double>(t) / sampleRate;

            // Soft attack (quarter sine: zero slope at t=0, no click).
            double env = 1.0;
            const double attackSamples = attackTime * sampleRate;
            if (t < attackSamples)
                env = std::sin((M_PI / 2.0) * (t / attackSamples));
            env *= std::exp(-decayPerSec * time);

            const double sample = overallGain * note.amplitude * env * std::sin(phaseIncrement * t);

            data[i * 2] += static_cast<float>(sample);
            data[i * 2 + 1] += static_cast<float>(sample);
        }
    }

    // Fade the very end to exactly zero so playback ends silently.
    const int releaseSamples = static_cast<int>(releaseTime * sampleRate);
    for (int i = std::max(0, numSamples - releaseSamples); i < numSamples; ++i) {
        const double fade = 1.0 - (static_cast<double>(i - (numSamples - releaseSamples)) / releaseSamples);
        data[i * 2] *= static_cast<float>(fade);
        data[i * 2 + 1] *= static_cast<float>(fade);
    }

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
            // Stop current playback and release any buffer source first,
            // mirroring the buffer path below: leaving m_currentBuffer alive
            // while switching to a URL source would keep a stale buffer
            // attached (and its EndOfMedia cleanup lambda armed for a source
            // that no longer plays it).
            m_player->stop();
            if (m_currentBuffer) {
                m_currentBuffer->deleteLater();
                m_currentBuffer = nullptr;
            }
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

    // Stop current playback and release any buffer source before switching to
    // a URL source (see playNotificationSound's URL path for the same reason).
    m_player->stop();
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

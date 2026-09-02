#include "AudioPipeline.hpp"
#include "IAudioBackend.hpp"
#include "OpusEncoder.hpp"
#include "OpusDecoder.hpp"
#include "JitterBuffer.hpp"
#include "AudioMixer.hpp"
#include "NoiseSuppressor.hpp"

#include "Core/Logging.hpp"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Acheron {
namespace Core {
namespace AV {

// Defined in MiniaudioAudioBackend.cpp. Drains complete capture frames out of
// the backend's capture ring buffer; the mix tick runs this on the voice
// thread so the device callback stays allocation-free. Returns an empty list
// for non-miniaudio backends or when nothing is buffered.
QList<QByteArray> drainCaptureFrames(IAudioBackend *backend);

AudioPipeline::AudioPipeline(QObject *parent)
    : QObject(parent)
{
}

AudioPipeline::~AudioPipeline()
{
}

void AudioPipeline::start(IAudioBackend *backend, bool capturing)
{
    if (audioBackend)
        return;

    audioBackend = backend;
    // Kept for interface compatibility: MiniaudioAudioBackend no longer emits
    // audioCaptured — capture frames are drained from the mix tick instead
    // (drainCapturedAudio). If a future backend emits again, the queued
    // connection still delivers them through onAudioCaptured.
    connect(audioBackend, &IAudioBackend::audioCaptured, this, &AudioPipeline::onAudioCaptured, Qt::QueuedConnection);

    initializeEncoder();

    noiseSuppressor = std::make_unique<NoiseSuppressor>();
    if (!noiseSuppressor->init())
        noiseSuppressor.reset();

    rmsThrottleTimer.start();
    userRmsThrottleTimer.start();

    if (capturing)
        audioBackend->startCapture();
    reconfigureNoiseSuppressorChannels();

    audioBackend->startPlayback();

    mixTimer = new QTimer(this);
    mixTimer->setTimerType(Qt::PreciseTimer);
    mixTimer->setInterval(AUDIO_FRAME_DURATION_MS);
    connect(mixTimer, &QTimer::timeout, this, &AudioPipeline::onMixTick);
    mixTimer->start();

    qCInfo(LogVoice) << "Audio pipeline started";
}

void AudioPipeline::stop()
{
    delete mixTimer;
    mixTimer = nullptr;

    if (audioBackend) {
        disconnect(audioBackend, &IAudioBackend::audioCaptured, this, &AudioPipeline::onAudioCaptured);
        audioBackend->stopCapture();
        audioBackend->stopPlayback();
    }

    speakers.clear();
    ssrcToUser.clear();
    pendingUserRms.clear();

    encoder.reset();
    noiseSuppressor.reset();

    audioBackend = nullptr;

    isSpeaking = false;
    vadHoldoffCounter = 0;

    qCDebug(LogVoice) << "Audio pipeline stopped";
}

void AudioPipeline::startCapture()
{
    if (!audioBackend)
        return;

    audioBackend->startCapture();
    reconfigureNoiseSuppressorChannels();
}

void AudioPipeline::stopCapture()
{
    if (!audioBackend)
        return;

    audioBackend->stopCapture();

    if (isSpeaking) {
        sendTrailingSilence();
        isSpeaking = false;
        vadHoldoffCounter = 0;
        emit speakingChanged(false);
    }
}

void AudioPipeline::onAudioReceived(quint32 ssrc, uint16_t sequence, uint32_t /*timestamp*/, const QByteArray &opusData)
{
    auto it = speakers.find(ssrc);
    if (it == speakers.end()) {
        // Bound unmapped speakers before creating yet another one: drop idle
        // stragglers first, then evict the least recently used unmapped speaker
        // if the table is still full. Mapped speakers are never evicted here;
        // they are owned by removeUser().
        if (speakers.size() >= MAX_SPEAKERS) {
            evictIdleUnmappedSpeakers();
            if (speakers.size() >= MAX_SPEAKERS && !evictLeastRecentlyUsedUnmappedSpeaker()) {
                qCWarning(LogVoice) << "Speaker table full with only mapped speakers;"
                                    << "dropping packet from unknown SSRC" << ssrc;
                return;
            }
        }

        SpeakerState state;
        state.decoder = std::make_unique<OpusDecoder>();
        if (!state.decoder->init(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS)) {
            qCWarning(LogVoice) << "Failed to init decoder for SSRC" << ssrc;
            return;
        }
        state.jitterBuffer = std::make_unique<JitterBuffer>();

        auto [inserted, _] = speakers.emplace(ssrc, std::move(state));
        it = inserted;
    }

    it->second.lastActivityMs = QDateTime::currentMSecsSinceEpoch();
    it->second.jitterBuffer->push(sequence, opusData);
}

void AudioPipeline::evictIdleUnmappedSpeakers()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = speakers.begin(); it != speakers.end();) {
        const quint32 ssrc = it->first;
        const bool unmapped = !ssrcToUser.contains(ssrc);
        if (unmapped && now - it->second.lastActivityMs > UNMAPPED_SPEAKER_IDLE_TIMEOUT_MS) {
            qCInfo(LogVoice) << "Evicting idle unmapped speaker with SSRC" << ssrc;
            it = speakers.erase(it);
        } else {
            ++it;
        }
    }
}

bool AudioPipeline::evictLeastRecentlyUsedUnmappedSpeaker()
{
    auto oldest = speakers.end();
    for (auto it = speakers.begin(); it != speakers.end(); ++it) {
        if (ssrcToUser.contains(it->first))
            continue;
        if (oldest == speakers.end() || it->second.lastActivityMs < oldest->second.lastActivityMs)
            oldest = it;
    }

    if (oldest == speakers.end())
        return false;

    qCInfo(LogVoice) << "Evicting least recently used unmapped speaker with SSRC" << oldest->first;
    speakers.erase(oldest);
    return true;
}

void AudioPipeline::setDeafened(bool deafened)
{
    this->deafened = deafened;
}

void AudioPipeline::setSsrcUserId(quint32 ssrc, Snowflake userId)
{
    ssrcToUser[ssrc] = userId;
}

void AudioPipeline::removeUser(Snowflake userId)
{
    for (auto it = ssrcToUser.begin(); it != ssrcToUser.end();) {
        if (it.value() == userId) {
            speakers.erase(it.key());
            it = ssrcToUser.erase(it);
        } else {
            ++it;
        }
    }
    userVolumes.remove(userId);
    pendingUserRms.remove(userId);
}

void AudioPipeline::setUserVolume(Snowflake userId, float volume)
{
    if (volume == 1.0f)
        userVolumes.remove(userId);
    else
        userVolumes.insert(userId, volume);
}

void AudioPipeline::setInputDevice(const QByteArray &deviceId)
{
    if (!audioBackend)
        return;

    audioBackend->setInputDevice(deviceId);
    reconfigureNoiseSuppressorChannels();
}

void AudioPipeline::reconfigureNoiseSuppressorChannels()
{
    if (noiseSuppressor && audioBackend && audioBackend->isCapturing())
        noiseSuppressor->reconfigure(audioBackend->nativeCaptureChannels());
}

void AudioPipeline::setOutputDevice(const QByteArray &deviceId)
{
    if (audioBackend)
        audioBackend->setOutputDevice(deviceId);
}

void AudioPipeline::setInputGain(float gain)
{
    if (audioBackend)
        audioBackend->setInputGain(gain);
}

void AudioPipeline::setOutputVolume(float volume)
{
    if (audioBackend)
        audioBackend->setOutputVolume(volume);
}

void AudioPipeline::setVadThreshold(float threshold)
{
    vadThreshold = threshold;
}

void AudioPipeline::setVadSensitivity(float percent)
{
    vadSensitivity = std::clamp(percent, 0.0f, 100.0f);
}

void AudioPipeline::setNoiseSuppressionEnabled(bool enabled)
{
    noiseSuppressionEnabled = enabled;
}

void AudioPipeline::setUseRnnoiseVad(bool enabled)
{
    useRnnoiseVad = enabled;
}

void AudioPipeline::initializeEncoder()
{
    encoder = std::make_unique<OpusEncoder>();
    if (!encoder->init(AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, opusApplication)) {
        qCCritical(LogVoice) << "Failed to initialize Opus encoder";
        encoder.reset();
        return;
    }
    encoder->setBitrate(opusBitrate);
    encoder->setComplexity(opusComplexity);
    encoder->setSignalType(opusSignalType);
    encoder->setFec(opusFec);
    encoder->setPacketLossPercent(opusPacketLossPercent);
}

void AudioPipeline::setOpusApplication(int application)
{
    if (opusApplication == application)
        return;

    opusApplication = application;

    if (encoder)
        initializeEncoder();
}

void AudioPipeline::setOpusBitrate(int bitrate)
{
    opusBitrate = bitrate;

    if (encoder)
        encoder->setBitrate(bitrate);
}

void AudioPipeline::setOpusComplexity(int complexity)
{
    opusComplexity = complexity;

    if (encoder)
        encoder->setComplexity(complexity);
}

void AudioPipeline::setOpusSignalType(int signalType)
{
    opusSignalType = signalType;

    if (encoder)
        encoder->setSignalType(signalType);
}

void AudioPipeline::setOpusFec(bool enabled)
{
    opusFec = enabled;

    if (encoder)
        encoder->setFec(enabled);
}

void AudioPipeline::setOpusPacketLossPercent(int percent)
{
    opusPacketLossPercent = percent;

    if (encoder)
        encoder->setPacketLossPercent(percent);
}

void AudioPipeline::setPushToTalkEnabled(bool enabled)
{
    pushToTalkEnabled = enabled;
}

void AudioPipeline::setPushToTalkKeyHeld(bool held)
{
    pushToTalkKeyHeld = held;
}

void AudioPipeline::onAudioCaptured(const QByteArray &pcmData)
{
    // Interface-compatibility entry point: MiniaudioAudioBackend no longer
    // emits, but any backend that does still lands here.
    processCapturedFrame(pcmData);
}

void AudioPipeline::drainCapturedAudio()
{
    if (!audioBackend)
        return;

    const QList<QByteArray> frames = drainCaptureFrames(audioBackend);
    for (const QByteArray &frame : frames)
        processCapturedFrame(frame);
}

void AudioPipeline::processCapturedFrame(const QByteArray &pcmData)
{
    if (!encoder)
        return;

    // Push-to-talk gate: when PTT is enabled, transmit only while the bound
    // key is held. The recorder keeps running regardless; this gates the
    // send path so frames are dropped (and any open transmission closed)
    // while the key is up.
    if (pushToTalkEnabled && !pushToTalkKeyHeld) {
        if (isSpeaking) {
            sendTrailingSilence();
            isSpeaking = false;
            vadHoldoffCounter = 0;
            emit speakingChanged(false);
        }
        return;
    }

    QByteArray frame = pcmData;
    float voiceProb = -1.0f;
    if (noiseSuppressor && (noiseSuppressionEnabled || useRnnoiseVad)) {
        QByteArray denoised = noiseSuppressor->process(pcmData, voiceProb);
        if (noiseSuppressionEnabled)
            frame = denoised;
    }

    float rms = 0.0f;
    bool rmsVoice = detectVoiceActivity(frame, rms);

    bool voiceDetected;
    if (useRnnoiseVad && voiceProb >= 0.0f)
        voiceDetected = voiceProb > vadProbabilityThreshold;
    else
        voiceDetected = rmsVoice;

    if (rmsThrottleTimer.elapsed() >= RMS_EMIT_INTERVAL_MS) {
        emit audioLevelChanged(rms);
        rmsThrottleTimer.restart();
    }

    if (voiceDetected) {
        vadHoldoffCounter = vadHoldoffFrames;
        if (!isSpeaking) {
            isSpeaking = true;
            emit speakingChanged(true);
        }
    } else if (vadHoldoffCounter > 0) {
        vadHoldoffCounter--;
    } else if (isSpeaking) {
        sendTrailingSilence();
        isSpeaking = false;
        emit speakingChanged(false);
        return;
    }

    if (!isSpeaking)
        return;

    QByteArray encoded = encoder->encode(frame);
    if (!encoded.isEmpty())
        emit encodedAudioReady(encoded);
}

void AudioPipeline::onMixTick()
{
    // Drain captured audio first: the recorder runs independently of the
    // playback/deafen state (previously, queued audioCaptured events were
    // processed regardless of it). Draining here keeps the device callback
    // free of allocations, event posting and mutex traffic.
    drainCapturedAudio();

    if (deafened || !audioBackend)
        return;

    // Speakers that simply stopped sending never trigger onAudioReceived, so
    // reclaim idle unmapped ones from the mix tick as well.
    evictIdleUnmappedSpeakers();

    QVector<std::pair<QByteArray, float>> streams;

    for (auto it = speakers.begin(); it != speakers.end(); ++it) {
        quint32 ssrc = it->first;
        SpeakerState &state = it->second;

        QByteArray pcm;

        if (!state.pendingFrames.isEmpty()) {
            pcm = state.pendingFrames.takeFirst();
        } else {
            if (!state.jitterBuffer->isReady())
                continue;

            QByteArray opusData = state.jitterBuffer->pop();

            if (opusData.isEmpty()) {
                pcm = state.decoder->decodePlc();
            } else {
                QVector<QByteArray> frames = state.decoder->decode(opusData);
                if (frames.isEmpty())
                    continue;
                pcm = frames.first();
                for (int i = 1; i < frames.size(); i++) {
                    if (state.pendingFrames.size() >= MAX_PENDING_FRAMES_PER_SPEAKER)
                        break;
                    state.pendingFrames.append(frames[i]);
                }
                // The remaining frames of this packet play from pendingFrames on
                // the next N-1 ticks without calling pop(); fast-forward the
                // jitter buffer over them so its sequence tracking stays aligned
                // with the frames actually consumed (see advanceFrames).
                state.jitterBuffer->advanceFrames(frames.size() - 1);
            }
        }

        if (pcm.isEmpty())
            continue;

        state.lastActivityMs = QDateTime::currentMSecsSinceEpoch();

        // rms before gain
        auto userIt = ssrcToUser.constFind(ssrc);
        Snowflake userId;
        if (userIt != ssrcToUser.constEnd())
            userId = userIt.value();

        if (userId.isValid()) {
            const auto *samples = reinterpret_cast<const int16_t *>(pcm.constData());
            int count = pcm.size() / static_cast<int>(sizeof(int16_t));
            if (count > 0) {
                float rms = computeRms(samples, count);

                auto rmsIt = pendingUserRms.find(userId);
                if (rmsIt == pendingUserRms.end())
                    pendingUserRms.insert(userId, rms);
                else if (rms > rmsIt.value())
                    rmsIt.value() = rms;
            }
        }

        float gain = 1.0f;
        if (userId.isValid())
            gain = userVolumes.value(userId, 1.0f);

        streams.append({ pcm, gain });
    }

    // emit periodically
    if (userRmsThrottleTimer.elapsed() >= RMS_EMIT_INTERVAL_MS) {
        for (auto it = pendingUserRms.constBegin(); it != pendingUserRms.constEnd(); ++it)
            emit userAudioLevelChanged(it.key(), it.value());
        pendingUserRms.clear();
        userRmsThrottleTimer.restart();
    }

    if (streams.isEmpty())
        return;

    QByteArray mixed = AudioMixer::mix(streams);
    if (mixed.isEmpty())
        return;

    audioBackend->pushPlaybackFrame(reinterpret_cast<const int16_t *>(mixed.constData()));
}

bool AudioPipeline::detectVoiceActivity(const QByteArray &pcmFrame, float &outRms) const
{
    const auto *samples = reinterpret_cast<const int16_t *>(pcmFrame.constData());
    int count = pcmFrame.size() / static_cast<int>(sizeof(int16_t));
    if (count == 0) {
        outRms = 0.0f;
        return false;
    }

    outRms = computeRms(samples, count);

    // Sensitivity scales the base threshold: 50% is neutral, 100% is 4x more
    // sensitive (threshold / 4), 0% is 4x less sensitive (threshold * 4).
    const float sensitivityScale = std::pow(2.0f, 2.0f - 4.0f * (vadSensitivity / 100.0f));
    return outRms > vadThreshold * sensitivityScale;
}

float AudioPipeline::computeRms(const int16_t *samples, int count)
{
    double sum = 0;
    for (int i = 0; i < count; i++)
        sum += static_cast<double>(samples[i]) * samples[i];
    return static_cast<float>(std::sqrt(sum / count));
}

void AudioPipeline::sendTrailingSilence()
{
    QByteArray silence(reinterpret_cast<const char *>(OPUS_SILENCE), sizeof(OPUS_SILENCE));
    for (int i = 0; i < TRAILING_SILENCE_FRAMES; i++)
        emit encodedAudioReady(silence);
}

} // namespace AV
} // namespace Core
} // namespace Acheron

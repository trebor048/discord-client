#define MINIAUDIO_IMPLEMENTATION
#include "Core/AV/Miniaudio.hpp"

#include "MiniaudioAudioBackend.hpp"

#include "Core/AV/AudioBackends.hpp"
#include "Core/Logging.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Acheron {
namespace Core {
namespace AV {

static constexpr ma_uint32 PLAYBACK_RB_FRAMES = AUDIO_FRAME_SAMPLES * 8;
// 8 frames of slack absorbs event-loop jitter between the device callback
// (producer) and the mix-tick drain (consumer) without dropping audio.
static constexpr ma_uint32 CAPTURE_RB_FRAMES = AUDIO_FRAME_SAMPLES * 8;

struct MiniaudioState
{
    ma_log log = {};
    ma_context context = {};
    ma_device captureDevice = {};
    ma_device playbackDevice = {};
    ma_pcm_rb playbackRB = {};
    ma_pcm_rb captureRB = {};
    bool logInit = false;
    bool contextInit = false;
    bool captureDeviceInit = false;
    bool playbackDeviceInit = false;
    bool playbackRBInit = false;
    bool captureRBInit = false;
};

namespace {

// The capture ring buffer lives in MiniaudioState (private to this TU), but
// AudioPipeline drains it from the mix tick. MiniaudioAudioBackend.hpp has no
// pull API, so the pipeline reaches it through the free function below, which
// reads these file-scope handles. Only one backend instance exists in the app
// (VoiceManager owns the single IAudioBackend::create() instance); the owner
// check keeps a stale or foreign backend from being drained.
MiniaudioAudioBackend *sCaptureOwner = nullptr;
ma_pcm_rb *sCaptureRB = nullptr;

} // namespace

void OnCapture(ma_device *pDevice, void *, const void *pInput, ma_uint32 frameCount)
{
    static_cast<MiniaudioAudioBackend *>(pDevice->pUserData)->handleCapturedFrames(pInput, frameCount);
}

void OnPlayback(ma_device *pDevice, void *pOutput, const void *, ma_uint32 frameCount)
{
    static_cast<MiniaudioAudioBackend *>(pDevice->pUserData)->handlePlaybackFrames(pOutput, frameCount);
}

static void MiniaudioLogCallback(void *pUserData, ma_uint32 level, const char *pMessage)
{
    Q_UNUSED(pUserData);

    QString msg = QString::fromUtf8(pMessage).trimmed();
    if (msg.isEmpty())
        return;

    switch (level) {
    case MA_LOG_LEVEL_ERROR:
        qCCritical(LogMiniaudio).noquote() << msg;
        break;
    case MA_LOG_LEVEL_WARNING:
        qCWarning(LogMiniaudio).noquote() << msg;
        break;
    case MA_LOG_LEVEL_INFO:
        qCInfo(LogMiniaudio).noquote() << msg;
        break;
    case MA_LOG_LEVEL_DEBUG:
    default:
        qCDebug(LogMiniaudio).noquote() << msg;
        break;
    }
}

static QByteArray SerializeDeviceId(const ma_device_id &id)
{
    return QByteArray(reinterpret_cast<const char *>(&id), sizeof(ma_device_id));
}

static bool DeserializeDeviceId(const QByteArray &bytes, ma_device_id &id)
{
    if (bytes.size() != sizeof(ma_device_id))
        return false;
    std::memcpy(&id, bytes.constData(), sizeof(ma_device_id));
    return true;
}

static QList<AudioDeviceInfo> EnumerateDevices(ma_context *ctx, ma_device_type type)
{
    QList<AudioDeviceInfo> result;

    ma_device_info *pPlayback = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info *pCapture = nullptr;
    ma_uint32 captureCount = 0;

    if (ma_context_get_devices(ctx, &pPlayback, &playbackCount, &pCapture, &captureCount) != MA_SUCCESS)
        return result;

    ma_device_info *infos = (type == ma_device_type_capture) ? pCapture : pPlayback;
    ma_uint32 count = (type == ma_device_type_capture) ? captureCount : playbackCount;

    for (ma_uint32 i = 0; i < count; i++) {
        AudioDeviceInfo info;
        info.id = SerializeDeviceId(infos[i].id);
        info.description = QString::fromUtf8(infos[i].name);
        info.isDefault = infos[i].isDefault != 0;
        result.append(info);
    }

    return result;
}

MiniaudioAudioBackend::MiniaudioAudioBackend(QObject *parent)
    : IAudioBackend(parent),
      ma(std::make_unique<MiniaudioState>())
{
    if (ma_log_init(nullptr, &ma->log) == MA_SUCCESS) {
        ma->logInit = true;
        ma_log_register_callback(&ma->log, ma_log_callback_init(MiniaudioLogCallback, nullptr));
    }

    ma_context_config contextConfig = ma_context_config_init();
    if (ma->logInit)
        contextConfig.pLog = &ma->log;

    if (!initAudioContext(&ma->context, &contextConfig))
        qCWarning(LogVoice) << "Failed to initialize miniaudio context";
    else
        ma->contextInit = true;

    if (ma_pcm_rb_init(ma_format_s16, AUDIO_CHANNELS, PLAYBACK_RB_FRAMES, NULL, NULL, &ma->playbackRB) != MA_SUCCESS)
        qCWarning(LogVoice) << "Failed to init playback ring buffer";
    else
        ma->playbackRBInit = true;

    if (ma_pcm_rb_init(ma_format_s16, AUDIO_CHANNELS, CAPTURE_RB_FRAMES, NULL, NULL, &ma->captureRB) != MA_SUCCESS) {
        qCWarning(LogVoice) << "Failed to init capture ring buffer";
    } else {
        ma->captureRBInit = true;
        sCaptureRB = &ma->captureRB;
        sCaptureOwner = this;
    }
}

MiniaudioAudioBackend::~MiniaudioAudioBackend()
{
    stopCapture();
    stopPlayback();

    if (sCaptureOwner == this) {
        sCaptureOwner = nullptr;
        sCaptureRB = nullptr;
    }

    if (ma->captureRBInit)
        ma_pcm_rb_uninit(&ma->captureRB);

    if (ma->playbackRBInit)
        ma_pcm_rb_uninit(&ma->playbackRB);

    if (ma->contextInit)
        ma_context_uninit(&ma->context);

    if (ma->logInit)
        ma_log_uninit(&ma->log);
}

QList<AudioDeviceInfo> MiniaudioAudioBackend::availableInputDevices() const
{
    if (!ma->contextInit)
        return {};
    return EnumerateDevices(&ma->context, ma_device_type_capture);
}

QList<AudioDeviceInfo> MiniaudioAudioBackend::availableOutputDevices() const
{
    if (!ma->contextInit)
        return {};
    return EnumerateDevices(&ma->context, ma_device_type_playback);
}

void MiniaudioAudioBackend::setInputDevice(const QByteArray &deviceId)
{
    if (deviceId == selectedInputId)
        return;

    selectedInputId = deviceId;
    qCInfo(LogVoice) << "Input device changed";

    if (ma->captureDeviceInit) {
        stopCapture();
        startCapture();
    }
}

void MiniaudioAudioBackend::setOutputDevice(const QByteArray &deviceId)
{
    if (deviceId == selectedOutputId)
        return;

    selectedOutputId = deviceId;
    qCInfo(LogVoice) << "Output device changed";

    if (ma->playbackDeviceInit) {
        stopPlayback();
        startPlayback();
    }
}

bool MiniaudioAudioBackend::startCapture()
{
    if (ma->captureDeviceInit)
        return true;

    if (!ma->contextInit)
        return false;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_s16;
    config.capture.channels = AUDIO_CHANNELS;
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.dataCallback = OnCapture;
    config.pUserData = this;
    config.periodSizeInFrames = AUDIO_FRAME_SAMPLES;

    ma_device_id deviceId;
    if (!selectedInputId.isEmpty() && DeserializeDeviceId(selectedInputId, deviceId))
        config.capture.pDeviceID = &deviceId;

    ma_result initResult = ma_device_init(&ma->context, &config, &ma->captureDevice);
    if (initResult != MA_SUCCESS) {
        qCWarning(LogVoice) << "Failed to init miniaudio capture device:" << ma_result_description(initResult);
        return false;
    }

    if (ma->captureRBInit)
        ma_pcm_rb_reset(&ma->captureRB);

    if (ma_device_start(&ma->captureDevice) != MA_SUCCESS) {
        qCWarning(LogVoice) << "Failed to start miniaudio capture device";
        ma_device_uninit(&ma->captureDevice);
        return false;
    }

    ma->captureDeviceInit = true;
    selectedInputId = SerializeDeviceId(ma->captureDevice.capture.id);
    qCInfo(LogVoice) << "Miniaudio capture started:" << ma->captureDevice.capture.name;
    return true;
}

void MiniaudioAudioBackend::stopCapture()
{
    if (!ma->captureDeviceInit)
        return;

    // ma_device_uninit stops the device and waits for callbacks to finish
    ma_device_uninit(&ma->captureDevice);
    ma->captureDeviceInit = false;
    // Discard anything the callback left in the ring buffer so a later
    // startCapture() session begins from silence.
    if (ma->captureRBInit)
        ma_pcm_rb_reset(&ma->captureRB);

    qCInfo(LogVoice) << "Miniaudio capture stopped";
}

bool MiniaudioAudioBackend::startPlayback()
{
    if (ma->playbackDeviceInit)
        return true;

    if (!ma->contextInit || !ma->playbackRBInit)
        return false;

    ma_pcm_rb_reset(&ma->playbackRB);

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = AUDIO_CHANNELS;
    config.sampleRate = AUDIO_SAMPLE_RATE;
    config.dataCallback = OnPlayback;
    config.pUserData = this;
    config.periodSizeInFrames = AUDIO_FRAME_SAMPLES;

    ma_device_id deviceId;
    if (!selectedOutputId.isEmpty() && DeserializeDeviceId(selectedOutputId, deviceId))
        config.playback.pDeviceID = &deviceId;

    ma_result initResult = ma_device_init(&ma->context, &config, &ma->playbackDevice);
    if (initResult != MA_SUCCESS) {
        qCWarning(LogVoice) << "Failed to init miniaudio playback device:" << ma_result_description(initResult);
        return false;
    }

    if (ma_device_start(&ma->playbackDevice) != MA_SUCCESS) {
        qCWarning(LogVoice) << "Failed to start miniaudio playback device";
        ma_device_uninit(&ma->playbackDevice);
        return false;
    }

    ma->playbackDeviceInit = true;
    selectedOutputId = SerializeDeviceId(ma->playbackDevice.playback.id);
    qCInfo(LogVoice) << "Miniaudio playback started:" << ma->playbackDevice.playback.name;
    return true;
}

void MiniaudioAudioBackend::stopPlayback()
{
    if (!ma->playbackDeviceInit)
        return;

    // ma_device_uninit stops the device and waits for callbacks to finish
    ma_device_uninit(&ma->playbackDevice);
    ma->playbackDeviceInit = false;

    qCInfo(LogVoice) << "Miniaudio playback stopped";
}

bool MiniaudioAudioBackend::isCapturing() const
{
    return ma->captureDeviceInit;
}

bool MiniaudioAudioBackend::isPlaying() const
{
    return ma->playbackDeviceInit;
}

int MiniaudioAudioBackend::nativeCaptureChannels() const
{
    if (!ma->captureDeviceInit)
        return 0;
    return static_cast<int>(ma->captureDevice.capture.internalChannels);
}

void MiniaudioAudioBackend::setInputGain(float gain)
{
    inputGain.store(gain, std::memory_order_relaxed);
}

void MiniaudioAudioBackend::setOutputVolume(float volume)
{
    outputVolume.store(volume, std::memory_order_relaxed);
}

bool MiniaudioAudioBackend::pushPlaybackFrame(const int16_t *frame)
{
    if (!ma->playbackRBInit)
        return false;

    if (ma_pcm_rb_available_write(&ma->playbackRB) < AUDIO_FRAME_SAMPLES)
        return false;

    ma_uint32 written = 0;
    while (written < static_cast<ma_uint32>(AUDIO_FRAME_SAMPLES)) {
        ma_uint32 toWrite = AUDIO_FRAME_SAMPLES - written;
        void *writePtr;
        if (ma_pcm_rb_acquire_write(&ma->playbackRB, &toWrite, &writePtr) != MA_SUCCESS || toWrite == 0)
            return false;
        std::memcpy(writePtr, frame + written * AUDIO_CHANNELS, toWrite * AUDIO_CHANNELS * sizeof(int16_t));
        ma_pcm_rb_commit_write(&ma->playbackRB, toWrite);
        written += toWrite;
    }
    return true;
}

void MiniaudioAudioBackend::handleCapturedFrames(const void *input, unsigned int frameCount)
{
    if (!input || !ma->captureRBInit)
        return;

    // Write the device callback's samples straight into the capture ring
    // buffer: no heap allocation, no memmove, no queued-signal emit on the
    // audio thread. AudioPipeline's mix tick drains complete frames on the
    // voice thread. Gain is applied here so the drain is a plain copy.
    const float gain = inputGain.load(std::memory_order_relaxed);
    const auto *src = static_cast<const int16_t *>(input);
    ma_uint32 written = 0;
    while (written < frameCount) {
        ma_uint32 toWrite = frameCount - written;
        void *writePtr = nullptr;
        if (ma_pcm_rb_acquire_write(&ma->captureRB, &toWrite, &writePtr) != MA_SUCCESS || toWrite == 0) {
            // Ring buffer full: the consumer fell behind and will discard the
            // stale tail at the next drain; drop the rest of this callback.
            return;
        }
        auto *dst = static_cast<int16_t *>(writePtr);
        const ma_uint32 sampleCount = toWrite * AUDIO_CHANNELS;
        if (gain == 1.0f) {
            std::memcpy(dst, src + written * AUDIO_CHANNELS,
                        sampleCount * static_cast<ma_uint32>(sizeof(int16_t)));
        } else {
            for (ma_uint32 i = 0; i < sampleCount; i++) {
                const int32_t val = static_cast<int32_t>(
                        std::lround(src[written * AUDIO_CHANNELS + i] * gain));
                dst[i] = static_cast<int16_t>(std::clamp(val,
                                                         static_cast<int32_t>(INT16_MIN),
                                                         static_cast<int32_t>(INT16_MAX)));
            }
        }
        ma_pcm_rb_commit_write(&ma->captureRB, toWrite);
        written += toWrite;
    }
}

void MiniaudioAudioBackend::handlePlaybackFrames(void *output, unsigned int frameCount)
{
    auto *out = static_cast<int16_t *>(output);
    ma_uint32 remaining = frameCount;

    while (remaining > 0) {
        ma_uint32 toRead = remaining;
        void *readPtr;
        if (ma_pcm_rb_acquire_read(&ma->playbackRB, &toRead, &readPtr) != MA_SUCCESS || toRead == 0) {
            std::memset(out, 0, remaining * AUDIO_CHANNELS * sizeof(int16_t));
            break;
        }
        std::memcpy(out, readPtr, toRead * AUDIO_CHANNELS * sizeof(int16_t));
        ma_pcm_rb_commit_read(&ma->playbackRB, toRead);
        out += toRead * AUDIO_CHANNELS;
        remaining -= toRead;
    }

    float vol = outputVolume.load(std::memory_order_relaxed);
    if (vol != 1.0f) {
        auto *samples = static_cast<int16_t *>(output);
        int count = static_cast<int>(frameCount) * AUDIO_CHANNELS;
        for (int i = 0; i < count; i++) {
            int32_t val = static_cast<int32_t>(std::lround(samples[i] * vol));
            samples[i] = static_cast<int16_t>(std::clamp(val,
                                                         static_cast<int32_t>(INT16_MIN),
                                                         static_cast<int32_t>(INT16_MAX)));
        }
    }
}

// Drains complete capture frames (AUDIO_FRAME_SAMPLES each) out of the
// backend's capture ring buffer. Called from AudioPipeline::onMixTick on the
// voice/main thread; the device callback only writes, so this is the consumer
// half of a lock-free single-producer/single-consumer hand-off (same model as
// the playback ring buffer, in the opposite direction). Declared in
// AudioPipeline.cpp; defined here because only this TU knows MiniaudioState.
QList<QByteArray> drainCaptureFrames(IAudioBackend *backend)
{
    QList<QByteArray> frames;
    if (!sCaptureRB || sCaptureOwner != backend)
        return frames;

    ma_pcm_rb *rb = sCaptureRB;

    // If the consumer fell behind the device, keep only the newest frames
    // (drop the oldest), mirroring the old captureBuffer "drop excess" policy
    // which also kept the freshest 1-2 frames under overload.
    const ma_uint32 maxQueued = 2 * static_cast<ma_uint32>(AUDIO_FRAME_SAMPLES);
    if (ma_pcm_rb_available_read(rb) > maxQueued)
        ma_pcm_rb_seek_read(rb, ma_pcm_rb_available_read(rb) - maxQueued);

    while (ma_pcm_rb_available_read(rb) >= static_cast<ma_uint32>(AUDIO_FRAME_SAMPLES)) {
        QByteArray frame(AUDIO_FRAME_SIZE, Qt::Uninitialized);
        int written = 0;
        while (written < AUDIO_FRAME_SAMPLES) {
            ma_uint32 toRead = AUDIO_FRAME_SAMPLES - written;
            void *readPtr = nullptr;
            if (ma_pcm_rb_acquire_read(rb, &toRead, &readPtr) != MA_SUCCESS || toRead == 0)
                break;
            std::memcpy(frame.data() + written * AUDIO_CHANNELS * static_cast<int>(sizeof(int16_t)),
                        readPtr, toRead * AUDIO_CHANNELS * static_cast<int>(sizeof(int16_t)));
            ma_pcm_rb_commit_read(rb, toRead);
            written += static_cast<int>(toRead);
        }
        if (written < AUDIO_FRAME_SAMPLES)
            break; // safety: partial frame at the wrap; leave it for next drain
        frames.append(frame);
    }
    return frames;
}

} // namespace AV
} // namespace Core
} // namespace Acheron

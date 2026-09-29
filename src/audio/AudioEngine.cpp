#include "AudioEngine.h"

#include <QFile>
#include <QTimer>

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

namespace sb {

namespace {

constexpr ma_uint32 kSampleRate = 48000;
constexpr ma_uint32 kChannels = 2;
constexpr ma_uint32 kPeriodMs = 10;
constexpr ma_uint32 kScratchFrames = 1024;
// Voice passthrough jitter buffer: start playing once this much is buffered and
// drop old audio when more than kInputMaxLatencyFrames is waiting.
constexpr ma_uint32 kInputTargetFrames = kSampleRate * 30 / 1000;
constexpr ma_uint32 kInputMaxLatencyFrames = kSampleRate * 90 / 1000;
constexpr ma_uint32 kInputRingFrames = kSampleRate / 2;

QString describe(ma_result result)
{
    return QString::fromUtf8(ma_result_description(result));
}

void raisePeak(std::atomic<float> &peak, const float *samples, size_t count)
{
    float max = 0.0f;
    for (size_t i = 0; i < count; ++i)
        max = std::max(max, std::fabs(samples[i]));
    if (max > peak.load(std::memory_order_relaxed))
        peak.store(max, std::memory_order_relaxed);
}

// One playing instance of a sound. It is rendered independently on every
// open sink, each with its own decoder, so the outputs never wait on each
// other even though they run on different clocks.
struct Voice {
    VoiceId id = 0;
    int soundId = -1;
    std::shared_ptr<const QByteArray> encoded;
    ma_uint64 lengthFrames = 0;
    std::atomic<float> gain{1.0f};
    std::atomic<bool> paused{false};
    std::atomic<bool> looping{false};
    std::atomic<ma_uint64> cursor[SinkCount]{};
    std::atomic<bool> finished[SinkCount]{};
    bool attached[SinkCount]{};
};

// A voice as rendered on one sink. ma_decoder keeps pointers to itself, so a
// Track must never move once its decoder is initialised.
struct Track {
    std::shared_ptr<Voice> voice;
    ma_decoder decoder{};
    bool decoderReady = false;

    Track() = default;
    Track(const Track &) = delete;
    Track &operator=(const Track &) = delete;
    ~Track()
    {
        if (decoderReady)
            ma_decoder_uninit(&decoder);
    }
};

// Common part of every device's user data, used by the notification callback.
struct DeviceState {
    AudioEngine *engine = nullptr;
    std::atomic<bool> closing{false};
    std::atomic<int> generation{0}; // bumped on every open, to drop stale reports
    QString deviceName;             // only touched on the GUI thread
};

struct InputState : DeviceState {
    ma_device device{};
    ma_pcm_rb ring{};
    bool deviceReady = false;
    bool ringReady = false;
    std::atomic<float> peak{0.0f};
};

struct SinkState : DeviceState {
    Sink kind = Sink::Speakers;
    ma_device device{};
    bool open = false;

    std::mutex mutex; // guards tracks, input and inputPrimed
    std::vector<std::unique_ptr<Track>> tracks;
    InputState *input = nullptr;
    bool inputPrimed = false;

    std::atomic<float> volume{1.0f};
    std::atomic<bool> enabled{true};
    std::atomic<float> voiceVolume{1.0f};
    std::atomic<bool> muteVoiceWhilePlaying{false};
    std::atomic<float> peak{0.0f};
    std::vector<float> scratch;
};

struct RecorderState {
    ma_device device{};
    ma_encoder encoder{};
    bool deviceReady = false;
    bool encoderReady = false;
    std::atomic<ma_uint64> frames{0};
};

void mixTrack(SinkState &sink, Track &track, float *out, ma_uint32 frameCount, float sinkGain)
{
    Voice &voice = *track.voice;
    const int index = static_cast<int>(sink.kind);
    const float gain = sinkGain * voice.gain.load(std::memory_order_relaxed);

    ma_uint32 done = 0;
    int emptyReads = 0;
    while (done < frameCount) {
        const ma_uint32 want = std::min(frameCount - done, kScratchFrames);
        ma_uint64 got = 0;
        ma_decoder_read_pcm_frames(&track.decoder, sink.scratch.data(), want, &got);

        float *dst = out + static_cast<size_t>(done) * kChannels;
        const float *src = sink.scratch.data();
        for (size_t i = 0; i < got * kChannels; ++i)
            dst[i] += src[i] * gain;
        done += static_cast<ma_uint32>(got);
        voice.cursor[index].fetch_add(got, std::memory_order_relaxed);

        if (got < want) {
            // End of the stream. A loop that yields nothing twice in a row is
            // an empty or broken file: give up instead of spinning.
            emptyReads = got == 0 ? emptyReads + 1 : 0;
            if (voice.looping.load(std::memory_order_relaxed) && emptyReads < 2
                && ma_decoder_seek_to_pcm_frame(&track.decoder, 0) == MA_SUCCESS) {
                voice.cursor[index].store(0, std::memory_order_relaxed);
                continue;
            }
            voice.finished[index].store(true, std::memory_order_release);
            break;
        }
    }
}

void mixInput(SinkState &sink, float *out, ma_uint32 frameCount, bool soundsAudible)
{
    InputState *input = sink.input;
    ma_uint32 available = ma_pcm_rb_available_read(&input->ring);

    if (available > kInputMaxLatencyFrames) {
        ma_pcm_rb_seek_read(&input->ring, available - kInputTargetFrames);
        available = kInputTargetFrames;
    }
    if (!sink.inputPrimed) {
        if (available < std::max(kInputTargetFrames, frameCount))
            return;
        sink.inputPrimed = true;
    }
    if (available < frameCount)
        sink.inputPrimed = false; // underrun: play what we have, then rebuffer

    float gain = sink.voiceVolume.load(std::memory_order_relaxed);
    if (soundsAudible && sink.muteVoiceWhilePlaying.load(std::memory_order_relaxed))
        gain = 0.0f;

    ma_uint32 remaining = std::min(available, frameCount);
    ma_uint32 offset = 0;
    while (remaining > 0) {
        ma_uint32 chunk = remaining;
        void *buffer = nullptr;
        if (ma_pcm_rb_acquire_read(&input->ring, &chunk, &buffer) != MA_SUCCESS || chunk == 0)
            break;
        const float *src = static_cast<const float *>(buffer);
        float *dst = out + static_cast<size_t>(offset) * kChannels;
        for (size_t i = 0; i < static_cast<size_t>(chunk) * kChannels; ++i)
            dst[i] += src[i] * gain;
        ma_pcm_rb_commit_read(&input->ring, chunk);
        remaining -= chunk;
        offset += chunk;
    }
}

void notificationCallback(const ma_device_notification *notification)
{
    if (notification->type != ma_device_notification_type_stopped)
        return;
    auto *state = static_cast<DeviceState *>(notification->pDevice->pUserData);
    if (state->closing.load())
        return;
    // Runs on a miniaudio thread; hop to the GUI thread before touching Qt state.
    AudioEngine *engine = state->engine;
    const int generation = state->generation.load();
    QMetaObject::invokeMethod(
        engine,
        [engine, state, generation] {
            if (!state->closing.load() && state->generation.load() == generation)
                emit engine->deviceStopped(state->deviceName);
        },
        Qt::QueuedConnection);
}

void sinkCallback(ma_device *device, void *output, const void *, ma_uint32 frameCount)
{
    auto *sink = static_cast<SinkState *>(static_cast<DeviceState *>(device->pUserData));
    auto *out = static_cast<float *>(output); // miniaudio pre-silences this buffer
    const int index = static_cast<int>(sink->kind);
    const float sinkGain = sink->enabled.load(std::memory_order_relaxed)
                               ? sink->volume.load(std::memory_order_relaxed)
                               : 0.0f;

    std::lock_guard lock(sink->mutex);
    bool soundsAudible = false;
    for (auto &track : sink->tracks) {
        Voice &voice = *track->voice;
        if (voice.paused.load(std::memory_order_relaxed)
            || voice.finished[index].load(std::memory_order_relaxed))
            continue;
        soundsAudible = soundsAudible || sinkGain > 0.0f;
        mixTrack(*sink, *track, out, frameCount, sinkGain);
    }
    if (sink->input)
        mixInput(*sink, out, frameCount, soundsAudible);

    const size_t samples = static_cast<size_t>(frameCount) * kChannels;
    for (size_t i = 0; i < samples; ++i)
        out[i] = std::clamp(out[i], -1.0f, 1.0f);
    raisePeak(sink->peak, out, samples);
}

void inputCallback(ma_device *device, void *, const void *input, ma_uint32 frameCount)
{
    auto *state = static_cast<InputState *>(static_cast<DeviceState *>(device->pUserData));
    const auto *src = static_cast<const float *>(input);
    raisePeak(state->peak, src, static_cast<size_t>(frameCount) * kChannels);

    ma_uint32 remaining = frameCount;
    ma_uint32 offset = 0;
    while (remaining > 0) {
        ma_uint32 chunk = remaining;
        void *buffer = nullptr;
        if (ma_pcm_rb_acquire_write(&state->ring, &chunk, &buffer) != MA_SUCCESS || chunk == 0)
            break; // ring full: the reader is behind and will skip ahead
        std::memcpy(buffer, src + static_cast<size_t>(offset) * kChannels,
                    static_cast<size_t>(chunk) * kChannels * sizeof(float));
        ma_pcm_rb_commit_write(&state->ring, chunk);
        remaining -= chunk;
        offset += chunk;
    }
}

void recorderCallback(ma_device *device, void *, const void *input, ma_uint32 frameCount)
{
    auto *state = static_cast<RecorderState *>(device->pUserData);
    ma_uint64 written = 0;
    ma_encoder_write_pcm_frames(&state->encoder, input, frameCount, &written);
    state->frames.fetch_add(written, std::memory_order_relaxed);
}

bool initDecoder(const QByteArray &encoded, ma_decoder *decoder, ma_result *result)
{
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
    *result = ma_decoder_init_memory(encoded.constData(), static_cast<size_t>(encoded.size()),
                                     &config, decoder);
    return *result == MA_SUCCESS;
}

} // namespace

struct AudioEngine::Private {
    ma_context context{};
    bool contextReady = false;
    SinkState sinks[SinkCount];
    InputState input;
    RecorderState recorder;
    std::vector<std::shared_ptr<Voice>> voices;
    VoiceId nextVoiceId = 1;
    AudioConfig config;

    std::shared_ptr<Voice> voice(VoiceId id) const
    {
        for (const auto &v : voices) {
            if (v->id == id)
                return v;
        }
        return nullptr;
    }

    bool findDevice(ma_device_type type, const QString &name, ma_device_id *id)
    {
        ma_device_info *playback = nullptr;
        ma_device_info *capture = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount)
            != MA_SUCCESS)
            return false;
        const bool isPlayback = type == ma_device_type_playback;
        const ma_device_info *infos = isPlayback ? playback : capture;
        const ma_uint32 count = isPlayback ? playbackCount : captureCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            if (QString::fromUtf8(infos[i].name) == name) {
                *id = infos[i].id;
                return true;
            }
        }
        return false;
    }

    DeviceList devices(ma_device_type type)
    {
        DeviceList list;
        if (!contextReady)
            return list;
        ma_device_info *playback = nullptr;
        ma_device_info *capture = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount)
            != MA_SUCCESS)
            return list;
        const bool isPlayback = type == ma_device_type_playback;
        const ma_device_info *infos = isPlayback ? playback : capture;
        const ma_uint32 count = isPlayback ? playbackCount : captureCount;
        for (ma_uint32 i = 0; i < count; ++i) {
            const QString name = QString::fromUtf8(infos[i].name);
            if (!list.names.contains(name))
                list.names.append(name);
            if (infos[i].isDefault && list.defaultName.isEmpty())
                list.defaultName = name;
        }
        return list;
    }

    QString openSink(SinkState &sink, const QString &deviceName)
    {
        ma_device_id id;
        const bool explicitDevice = !deviceName.isEmpty();
        if (explicitDevice && !findDevice(ma_device_type_playback, deviceName, &id))
            return AudioEngine::tr("Output device \"%1\" was not found.").arg(deviceName);

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = explicitDevice ? &id : nullptr;
        config.playback.format = ma_format_f32;
        config.playback.channels = kChannels;
        config.sampleRate = kSampleRate;
        config.periodSizeInMilliseconds = kPeriodMs;
        config.performanceProfile = ma_performance_profile_low_latency;
        config.dataCallback = sinkCallback;
        config.notificationCallback = notificationCallback;
        config.pUserData = static_cast<DeviceState *>(&sink);
        sink.closing.store(false);
        ++sink.generation;
        config.pulse.pStreamNamePlayback = sink.kind == Sink::Speakers ? "Sounds (speakers)"
                                                                       : "Sounds (microphone)";

        sink.scratch.assign(static_cast<size_t>(kScratchFrames) * kChannels, 0.0f);
        ma_result result = ma_device_init(&context, &config, &sink.device);
        if (result != MA_SUCCESS)
            return AudioEngine::tr("Could not open output device \"%1\": %2")
                .arg(explicitDevice ? deviceName : AudioEngine::tr("default"), describe(result));
        result = ma_device_start(&sink.device);
        if (result != MA_SUCCESS) {
            ma_device_uninit(&sink.device);
            return AudioEngine::tr("Could not start output device \"%1\": %2")
                .arg(explicitDevice ? deviceName : AudioEngine::tr("default"), describe(result));
        }
        sink.open = true;
        sink.deviceName = explicitDevice ? deviceName
                                         : QString::fromUtf8(sink.device.playback.name);
        return {};
    }

    void closeSink(SinkState &sink)
    {
        sink.closing.store(true);
        if (sink.open) {
            ma_device_uninit(&sink.device); // waits for the callback to return
            sink.open = false;
        }
        std::lock_guard lock(sink.mutex);
        sink.tracks.clear();
    }
};

AudioEngine::AudioEngine(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
    , m_pollTimer(new QTimer(this))
{
    d->sinks[0].kind = Sink::Speakers;
    d->sinks[1].kind = Sink::Microphone;
    d->sinks[0].engine = this;
    d->sinks[1].engine = this;
    d->input.engine = this;
    m_pollTimer->setInterval(30);
    connect(m_pollTimer, &QTimer::timeout, this, &AudioEngine::poll);
}

AudioEngine::~AudioEngine()
{
    stopRecording();
    close();
    if (d->contextReady)
        ma_context_uninit(&d->context);
}

bool AudioEngine::initialize(QString *error)
{
    if (d->contextReady)
        return true;
    ma_context_config config = ma_context_config_init();
    config.pulse.pApplicationName = "Soundboard";
    const ma_result result = ma_context_init(nullptr, 0, &config, &d->context);
    if (result != MA_SUCCESS) {
        if (error)
            *error = tr("Could not initialise the audio system: %1").arg(describe(result));
        return false;
    }
    d->contextReady = true;
    return true;
}

QString AudioEngine::backendName() const
{
    return d->contextReady ? QString::fromUtf8(ma_get_backend_name(d->context.backend)) : QString();
}

DeviceList AudioEngine::playbackDevices() const
{
    return d->devices(ma_device_type_playback);
}

DeviceList AudioEngine::captureDevices() const
{
    return d->devices(ma_device_type_capture);
}

QStringList AudioEngine::open(const AudioConfig &config)
{
    close();
    d->config = config;
    QStringList problems;
    if (!d->contextReady)
        return {tr("The audio system is not initialised.")};

    if (QString problem = d->openSink(d->sinks[0], config.speakerDevice); !problem.isEmpty())
        problems << problem;
    if (!config.microphoneSinkDevice.isEmpty()) {
        if (QString problem = d->openSink(d->sinks[1], config.microphoneSinkDevice);
            !problem.isEmpty())
            problems << problem;
        else if (config.voicePassthrough) {
            if (QString problem = openInput(config.inputDevice, config.avoidInputDevices);
                !problem.isEmpty())
                problems << problem;
        }
    }
    return problems;
}

void AudioEngine::close()
{
    const bool hadVoices = !d->voices.empty();
    // Close the microphone sink before the input it reads from.
    d->closeSink(d->sinks[1]);
    d->closeSink(d->sinks[0]);
    closeInput();
    d->voices.clear();
    m_pollTimer->stop();
    if (hadVoices)
        emit voicesChanged();
}

bool AudioEngine::isSinkOpen(Sink sink) const
{
    return d->sinks[static_cast<int>(sink)].open;
}

QString AudioEngine::sinkDeviceName(Sink sink) const
{
    const SinkState &state = d->sinks[static_cast<int>(sink)];
    return state.open ? state.deviceName : QString();
}

bool AudioEngine::isInputOpen() const
{
    return d->input.deviceReady;
}

QString AudioEngine::inputDeviceName() const
{
    return d->input.deviceReady ? d->input.deviceName : QString();
}

void AudioEngine::setSinkVolume(Sink sink, float volume)
{
    d->sinks[static_cast<int>(sink)].volume.store(std::clamp(volume, 0.0f, 2.0f));
}

void AudioEngine::setSinkEnabled(Sink sink, bool enabled)
{
    d->sinks[static_cast<int>(sink)].enabled.store(enabled);
}

void AudioEngine::setVoiceVolume(float volume)
{
    d->sinks[1].voiceVolume.store(std::clamp(volume, 0.0f, 2.0f));
}

QString AudioEngine::setVoicePassthrough(bool enabled)
{
    d->config.voicePassthrough = enabled;
    if (!enabled) {
        closeInput();
        return {};
    }
    if (d->input.deviceReady || !d->sinks[1].open)
        return {};
    return openInput(d->config.inputDevice, d->config.avoidInputDevices);
}

void AudioEngine::setMuteVoiceWhilePlaying(bool mute)
{
    d->sinks[1].muteVoiceWhilePlaying.store(mute);
}

QString AudioEngine::openInput(const QString &deviceName, const QStringList &avoid)
{
    closeInput();
    InputState &input = d->input;

    QString name = deviceName;
    if (name.isEmpty()) {
        // "Default" must never resolve to our own virtual microphone, or the
        // microphone output would feed back into itself.
        const DeviceList captures = captureDevices();
        if (avoid.contains(captures.defaultName)) {
            for (const QString &candidate : captures.names) {
                if (!avoid.contains(candidate)
                    && !candidate.startsWith(QLatin1String("Monitor of "))) {
                    name = candidate;
                    break;
                }
            }
            if (name.isEmpty())
                return tr("No microphone is available for voice passthrough.");
        }
    } else if (avoid.contains(name)) {
        return tr("\"%1\" is the soundboard's own microphone and can't be used as the voice input.")
            .arg(name);
    }

    ma_device_id id;
    const bool explicitDevice = !name.isEmpty();
    if (explicitDevice && !d->findDevice(ma_device_type_capture, name, &id))
        return tr("Microphone \"%1\" was not found.").arg(name);

    ma_result result = ma_pcm_rb_init(ma_format_f32, kChannels, kInputRingFrames, nullptr, nullptr,
                                      &input.ring);
    if (result != MA_SUCCESS)
        return tr("Could not allocate the microphone buffer: %1").arg(describe(result));
    input.ringReady = true;

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.pDeviceID = explicitDevice ? &id : nullptr;
    config.capture.format = ma_format_f32;
    config.capture.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.periodSizeInMilliseconds = kPeriodMs;
    config.performanceProfile = ma_performance_profile_low_latency;
    config.dataCallback = inputCallback;
    config.notificationCallback = notificationCallback;
    config.pUserData = static_cast<DeviceState *>(&input);
    input.closing.store(false);
    ++input.generation;
    config.pulse.pStreamNameCapture = "Voice passthrough";

    result = ma_device_init(&d->context, &config, &input.device);
    if (result != MA_SUCCESS) {
        closeInput();
        return tr("Could not open microphone \"%1\": %2")
            .arg(explicitDevice ? name : tr("default"), describe(result));
    }
    input.deviceReady = true;
    result = ma_device_start(&input.device);
    if (result != MA_SUCCESS) {
        closeInput();
        return tr("Could not start microphone \"%1\": %2")
            .arg(explicitDevice ? name : tr("default"), describe(result));
    }
    input.deviceName = explicitDevice ? name : QString::fromUtf8(input.device.capture.name);

    std::lock_guard lock(d->sinks[1].mutex);
    d->sinks[1].input = &input;
    d->sinks[1].inputPrimed = false;
    return {};
}

void AudioEngine::closeInput()
{
    InputState &input = d->input;
    input.closing.store(true);
    {
        std::lock_guard lock(d->sinks[1].mutex);
        d->sinks[1].input = nullptr;
    }
    if (input.deviceReady) {
        ma_device_uninit(&input.device);
        input.deviceReady = false;
    }
    if (input.ringReady) {
        ma_pcm_rb_uninit(&input.ring);
        input.ringReady = false;
    }
    input.deviceName.clear();
}

VoiceId AudioEngine::play(int soundId, std::shared_ptr<const QByteArray> encoded, float gain,
                          bool loop, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return VoiceId(0);
    };
    if (!encoded || encoded->isEmpty())
        return fail(tr("The file is empty."));
    if (!d->sinks[0].open && !d->sinks[1].open)
        return fail(tr("No audio output is open. Check the audio settings."));

    auto voice = std::make_shared<Voice>();
    voice->id = d->nextVoiceId++;
    voice->soundId = soundId;
    voice->encoded = encoded;
    voice->gain.store(gain);
    voice->looping.store(loop);

    std::unique_ptr<Track> tracks[SinkCount];
    bool lengthKnown = false;
    for (int i = 0; i < SinkCount; ++i) {
        if (!d->sinks[i].open)
            continue;
        auto track = std::make_unique<Track>();
        track->voice = voice;
        ma_result result;
        if (!initDecoder(*encoded, &track->decoder, &result))
            return fail(tr("The file format is not supported or the file is damaged."));
        track->decoderReady = true;
        if (!lengthKnown) {
            ma_uint64 length = 0;
            if (ma_decoder_get_length_in_pcm_frames(&track->decoder, &length) == MA_SUCCESS) {
                voice->lengthFrames = length;
                lengthKnown = true;
            }
        }
        voice->attached[i] = true;
        tracks[i] = std::move(track);
    }

    for (int i = 0; i < SinkCount; ++i) {
        if (!tracks[i])
            continue;
        std::lock_guard lock(d->sinks[i].mutex);
        d->sinks[i].tracks.push_back(std::move(tracks[i]));
    }
    d->voices.push_back(voice);
    m_pollTimer->start();
    emit voicesChanged();
    return voice->id;
}

void AudioEngine::removeVoice(VoiceId id)
{
    for (SinkState &sink : d->sinks) {
        std::vector<std::unique_ptr<Track>> removed;
        {
            std::lock_guard lock(sink.mutex);
            auto it = std::stable_partition(sink.tracks.begin(), sink.tracks.end(),
                                            [id](const auto &t) { return t->voice->id != id; });
            std::move(it, sink.tracks.end(), std::back_inserter(removed));
            sink.tracks.erase(it, sink.tracks.end());
        }
        // Decoders are destroyed here, outside the lock.
    }
    std::erase_if(d->voices, [id](const auto &v) { return v->id == id; });
}

void AudioEngine::stop(VoiceId id)
{
    const auto voice = d->voice(id);
    if (!voice)
        return;
    removeVoice(id);
    emit voiceFinished(id, voice->soundId);
    emit voicesChanged();
}

void AudioEngine::stopAll()
{
    const auto voices = d->voices;
    for (const auto &voice : voices) {
        removeVoice(voice->id);
        emit voiceFinished(voice->id, voice->soundId);
    }
    if (!voices.empty())
        emit voicesChanged();
}

void AudioEngine::setPaused(VoiceId id, bool paused)
{
    if (const auto voice = d->voice(id)) {
        voice->paused.store(paused);
        emit voicesChanged();
    }
}

void AudioEngine::setAllPaused(bool paused)
{
    for (const auto &voice : d->voices)
        voice->paused.store(paused);
    emit voicesChanged();
}

bool AudioEngine::isPaused(VoiceId id) const
{
    const auto voice = d->voice(id);
    return voice && voice->paused.load();
}

void AudioEngine::setLooping(VoiceId id, bool loop)
{
    if (const auto voice = d->voice(id))
        voice->looping.store(loop);
}

void AudioEngine::setAllLooping(bool loop)
{
    for (const auto &voice : d->voices)
        voice->looping.store(loop);
}

void AudioEngine::seek(VoiceId id, double seconds)
{
    const auto voice = d->voice(id);
    if (!voice)
        return;
    ma_uint64 frame = static_cast<ma_uint64>(std::max(0.0, seconds) * kSampleRate);
    if (voice->lengthFrames > 0)
        frame = std::min(frame, voice->lengthFrames);
    for (int i = 0; i < SinkCount; ++i) {
        SinkState &sink = d->sinks[i];
        std::lock_guard lock(sink.mutex);
        for (auto &track : sink.tracks) {
            if (track->voice->id != id)
                continue;
            if (ma_decoder_seek_to_pcm_frame(&track->decoder, frame) == MA_SUCCESS) {
                voice->cursor[i].store(frame);
                voice->finished[i].store(false);
            }
        }
    }
}

double AudioEngine::position(VoiceId id) const
{
    const auto voice = d->voice(id);
    if (!voice)
        return 0.0;
    const int primary = voice->attached[0] ? 0 : 1;
    return static_cast<double>(voice->cursor[primary].load()) / kSampleRate;
}

double AudioEngine::duration(VoiceId id) const
{
    const auto voice = d->voice(id);
    return voice ? static_cast<double>(voice->lengthFrames) / kSampleRate : 0.0;
}

int AudioEngine::soundId(VoiceId id) const
{
    const auto voice = d->voice(id);
    return voice ? voice->soundId : -1;
}

bool AudioEngine::isActive(VoiceId id) const
{
    return d->voice(id) != nullptr;
}

QList<VoiceId> AudioEngine::voices() const
{
    QList<VoiceId> ids;
    for (const auto &voice : d->voices)
        ids.append(voice->id);
    return ids;
}

bool AudioEngine::isPlaying() const
{
    return std::any_of(d->voices.begin(), d->voices.end(),
                       [](const auto &v) { return !v->paused.load(); });
}

float AudioEngine::takeSinkLevel(Sink sink)
{
    return d->sinks[static_cast<int>(sink)].peak.exchange(0.0f);
}

float AudioEngine::takeInputLevel()
{
    return d->input.peak.exchange(0.0f);
}

void AudioEngine::poll()
{
    const auto voices = d->voices;
    bool changed = false;
    for (const auto &voice : voices) {
        bool done = true;
        for (int i = 0; i < SinkCount; ++i) {
            if (voice->attached[i] && !voice->finished[i].load(std::memory_order_acquire))
                done = false;
        }
        if (done) {
            removeVoice(voice->id);
            emit voiceFinished(voice->id, voice->soundId);
            changed = true;
        }
    }
    if (d->voices.empty())
        m_pollTimer->stop();
    if (changed)
        emit voicesChanged();
}

bool AudioEngine::startRecording(const QString &deviceName, bool loopback, const QString &filePath,
                                 QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    stopRecording();
    RecorderState &rec = d->recorder;
    if (!d->contextReady)
        return fail(tr("The audio system is not initialised."));
    if (loopback && !ma_context_is_loopback_supported(&d->context))
        return fail(tr("Recording the speakers is not supported by the %1 audio backend.")
                        .arg(backendName()));

    QString name = deviceName;
    if (name.isEmpty() && !loopback)
        name = d->config.inputDevice;
    ma_device_id id;
    const bool explicitDevice = !name.isEmpty();
    const ma_device_type lookup = loopback ? ma_device_type_playback : ma_device_type_capture;
    if (explicitDevice && !d->findDevice(lookup, name, &id))
        return fail(tr("Recording device \"%1\" was not found.").arg(name));

    ma_encoder_config encoderConfig =
        ma_encoder_config_init(ma_encoding_format_wav, ma_format_s16, kChannels, kSampleRate);
#ifdef Q_OS_WIN
    const std::wstring widePath = filePath.toStdWString();
    ma_result result = ma_encoder_init_file_w(widePath.c_str(), &encoderConfig, &rec.encoder);
#else
    ma_result result = ma_encoder_init_file(QFile::encodeName(filePath).constData(), &encoderConfig,
                                            &rec.encoder);
#endif
    if (result != MA_SUCCESS)
        return fail(tr("Could not create \"%1\": %2").arg(filePath, describe(result)));
    rec.encoderReady = true;

    ma_device_config config =
        ma_device_config_init(loopback ? ma_device_type_loopback : ma_device_type_capture);
    config.capture.pDeviceID = explicitDevice ? &id : nullptr;
    config.capture.format = ma_format_s16;
    config.capture.channels = kChannels;
    config.sampleRate = kSampleRate;
    config.dataCallback = recorderCallback;
    config.pUserData = &rec;
    config.pulse.pStreamNameCapture = "Recording";
    rec.frames.store(0);

    result = ma_device_init(&d->context, &config, &rec.device);
    if (result == MA_SUCCESS) {
        rec.deviceReady = true;
        result = ma_device_start(&rec.device);
    }
    if (result != MA_SUCCESS) {
        stopRecording();
        QFile::remove(filePath);
        return fail(tr("Could not start recording: %1").arg(describe(result)));
    }
    return true;
}

void AudioEngine::stopRecording()
{
    RecorderState &rec = d->recorder;
    if (rec.deviceReady) {
        ma_device_uninit(&rec.device);
        rec.deviceReady = false;
    }
    if (rec.encoderReady) {
        ma_encoder_uninit(&rec.encoder); // finalises the WAV header
        rec.encoderReady = false;
    }
}

bool AudioEngine::isRecording() const
{
    return d->recorder.deviceReady;
}

double AudioEngine::recordingSeconds() const
{
    return static_cast<double>(d->recorder.frames.load()) / kSampleRate;
}

bool AudioEngine::probeDuration(const QByteArray &encoded, double *seconds, QString *error)
{
    ma_decoder decoder;
    ma_result result;
    if (!initDecoder(encoded, &decoder, &result)) {
        if (error)
            *error = describe(result);
        return false;
    }
    ma_uint64 length = 0;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &length);
    ma_decoder_uninit(&decoder);
    if (result != MA_SUCCESS) {
        if (error)
            *error = describe(result);
        return false;
    }
    *seconds = static_cast<double>(length) / kSampleRate;
    return true;
}

QStringList AudioEngine::supportedExtensions()
{
    return {QStringLiteral("wav"), QStringLiteral("wave"), QStringLiteral("mp3"),
            QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("oga")};
}

QString AudioEngine::libraryVersion()
{
    return QStringLiteral(MA_VERSION_STRING);
}

} // namespace sb

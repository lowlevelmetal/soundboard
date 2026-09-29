#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QTimer;

namespace sb {

using VoiceId = quint64;

// The two places a sound can be heard. "Microphone" is the virtual cable that
// voice chat applications record from; it also carries the user's real voice.
enum class Sink { Speakers = 0, Microphone = 1 };
constexpr int SinkCount = 2;

struct DeviceList {
    QStringList names;
    QString defaultName;
};

struct AudioConfig {
    QString speakerDevice;          // empty: system default output
    QString microphoneSinkDevice;   // virtual cable input; empty: no microphone output
    QString inputDevice;            // real microphone for voice passthrough; empty: system default
    bool voicePassthrough = true;   // mix the real microphone into the microphone sink
    QStringList avoidInputDevices;  // never capture from these (our own virtual microphone)
};

// Plays decoded sounds to two output devices at once and mixes the user's
// microphone into the virtual microphone output. All public methods must be
// called from the GUI thread; the audio callbacks run on miniaudio's threads.
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    bool initialize(QString *error);
    QString backendName() const;

    DeviceList playbackDevices() const;
    DeviceList captureDevices() const;

    // (Re)opens all devices. Anything playing is stopped. Returns a list of
    // human readable problems; an empty list means every device opened.
    QStringList open(const AudioConfig &config);
    void close();

    bool isSinkOpen(Sink sink) const;
    QString sinkDeviceName(Sink sink) const;
    bool isInputOpen() const;
    QString inputDeviceName() const;

    void setSinkVolume(Sink sink, float volume);
    void setSinkEnabled(Sink sink, bool enabled);
    void setVoiceVolume(float volume);
    // Opens or closes the capture device without touching the outputs.
    QString setVoicePassthrough(bool enabled);
    void setMuteVoiceWhilePlaying(bool mute);

    VoiceId play(int soundId, std::shared_ptr<const QByteArray> encoded, float gain, bool loop,
                 QString *error);
    void stop(VoiceId id);
    void stopAll();
    void setPaused(VoiceId id, bool paused);
    void setAllPaused(bool paused);
    bool isPaused(VoiceId id) const;
    void setLooping(VoiceId id, bool loop);
    void setAllLooping(bool loop);
    void seek(VoiceId id, double seconds);
    double position(VoiceId id) const;
    double duration(VoiceId id) const;
    int soundId(VoiceId id) const;
    bool isActive(VoiceId id) const;
    QList<VoiceId> voices() const;   // oldest first
    bool isPlaying() const;

    // Peak level (0..1) since the previous call.
    float takeSinkLevel(Sink sink);
    float takeInputLevel();

    // Recording to a 16-bit WAV file. An empty device records from the voice
    // input device. On Windows "loopback" records what the speakers play.
    bool startRecording(const QString &deviceName, bool loopback, const QString &filePath,
                        QString *error);
    void stopRecording();
    bool isRecording() const;
    double recordingSeconds() const;

    static bool probeDuration(const QByteArray &encoded, double *seconds, QString *error);
    static QStringList supportedExtensions();
    static QString libraryVersion();

signals:
    void voiceFinished(sb::VoiceId id, int soundId);
    void voicesChanged();
    // An open device stopped on its own, e.g. because it was unplugged.
    void deviceStopped(const QString &deviceName);

private:
    struct Private;
    std::unique_ptr<Private> d;
    QTimer *m_pollTimer = nullptr;

    void poll();
    void removeVoice(VoiceId id);
    QString openInput(const QString &deviceName, const QStringList &avoid);
    void closeInput();
};

} // namespace sb

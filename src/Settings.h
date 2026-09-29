#pragma once

#include <QByteArray>
#include <QString>

namespace sb {

// User preferences, stored in an INI file in the platform config directory.
struct Settings {
    // recordingDevice value meaning "what the speakers play" (Windows only).
    static constexpr char LoopbackDevice[] = "@loopback";

    // Devices (empty means "system default"; for the microphone output it
    // means "automatic": our own virtual cable on Linux, VB-CABLE on Windows).
    QString speakerDevice;
    QString microphoneSinkDevice;
    QString inputDevice;
    QString recordingDevice; // empty: the voice input device

    bool playToSpeakers = true;
    bool playToMicrophone = true;
    int speakerVolume = 80;      // percent
    int microphoneVolume = 80;   // percent
    bool voicePassthrough = true;
    int voiceVolume = 100;       // percent
    bool muteVoiceWhilePlaying = false;

    bool allowOverlap = false;         // play several sounds at the same time
    bool hotkeyRestartsSound = true;   // otherwise the hotkey of a playing sound stops it
    bool loop = false;

    bool createVirtualMicrophone = true;       // Linux: create a PipeWire/PulseAudio virtual mic
    bool removeVirtualMicrophoneOnExit = true;

    bool pushToTalkEnabled = false;
    QString pushToTalkKey;             // QKeySequence::PortableText
    int pushToTalkReleaseDelayMs = 200;

    QString stopHotkey;
    QString pauseHotkey;
    QString randomHotkey;
    QString playSelectedHotkey;

    bool closeToTray = false;
    bool startMinimized = false;
    bool windowsCableHintShown = false;
    QString recordingsFolder;          // empty: <Music>/Soundboard Recordings

    static Settings load();
    void save() const;

    // Window geometry, header layout and similar opaque UI state.
    static QByteArray uiState(const QString &key);
    static void setUiState(const QString &key, const QByteArray &value);

    static QString configDirectory();
    static QString dataDirectory();
    QString effectiveRecordingsFolder() const;
};

} // namespace sb

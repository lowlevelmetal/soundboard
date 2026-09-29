#pragma once

#include "Settings.h"
#include "audio/AudioEngine.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

namespace sb {

class KeyCaptureEdit;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    SettingsDialog(const Settings &settings, const DeviceList &playback, const DeviceList &capture,
                   const QString &hotkeyStatus, bool loopbackSupported, QWidget *parent = nullptr);

    Settings settings() const;

private:
    QWidget *createAudioPage(const DeviceList &playback, const DeviceList &capture,
                             bool loopbackSupported);
    QWidget *createHotkeysPage(const QString &hotkeyStatus);
    QWidget *createPushToTalkPage();
    QWidget *createGeneralPage();

    Settings m_settings;

    QComboBox *m_speakerDevice = nullptr;
    QComboBox *m_microphoneSinkDevice = nullptr;
    QComboBox *m_inputDevice = nullptr;
    QComboBox *m_recordingDevice = nullptr;
    QCheckBox *m_createVirtualMic = nullptr;
    QCheckBox *m_removeVirtualMic = nullptr;
    QCheckBox *m_voicePassthrough = nullptr;
    QCheckBox *m_muteVoice = nullptr;

    QCheckBox *m_allowOverlap = nullptr;
    QComboBox *m_hotkeyBehaviour = nullptr;
    KeyCaptureEdit *m_stopHotkey = nullptr;
    KeyCaptureEdit *m_pauseHotkey = nullptr;
    KeyCaptureEdit *m_randomHotkey = nullptr;
    KeyCaptureEdit *m_playSelectedHotkey = nullptr;

    QCheckBox *m_pttEnabled = nullptr;
    KeyCaptureEdit *m_pttKey = nullptr;
    QSpinBox *m_pttDelay = nullptr;

    QCheckBox *m_closeToTray = nullptr;
    QCheckBox *m_startMinimized = nullptr;
    QLineEdit *m_recordingsFolder = nullptr;
};

} // namespace sb

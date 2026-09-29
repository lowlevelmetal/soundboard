#include "Settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace sb {

namespace {

QSettings openSettings()
{
    return QSettings(Settings::configDirectory() + QLatin1String("/settings.ini"),
                     QSettings::IniFormat);
}

} // namespace

QString Settings::configDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString Settings::dataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString Settings::effectiveRecordingsFolder() const
{
    if (!recordingsFolder.isEmpty())
        return recordingsFolder;
    QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    if (music.isEmpty())
        music = QDir::homePath();
    return music + QLatin1String("/Soundboard Recordings");
}

QByteArray Settings::uiState(const QString &key)
{
    return openSettings().value(QLatin1String("state/") + key).toByteArray();
}

void Settings::setUiState(const QString &key, const QByteArray &value)
{
    openSettings().setValue(QLatin1String("state/") + key, value);
}

Settings Settings::load()
{
    QSettings s = openSettings();
    Settings v;

    s.beginGroup(QStringLiteral("audio"));
    v.speakerDevice = s.value("speakerDevice").toString();
    v.microphoneSinkDevice = s.value("microphoneSinkDevice").toString();
    v.inputDevice = s.value("inputDevice").toString();
    v.recordingDevice = s.value("recordingDevice").toString();
    v.playToSpeakers = s.value("playToSpeakers", v.playToSpeakers).toBool();
    v.playToMicrophone = s.value("playToMicrophone", v.playToMicrophone).toBool();
    v.speakerVolume = s.value("speakerVolume", v.speakerVolume).toInt();
    v.microphoneVolume = s.value("microphoneVolume", v.microphoneVolume).toInt();
    v.voicePassthrough = s.value("voicePassthrough", v.voicePassthrough).toBool();
    v.voiceVolume = s.value("voiceVolume", v.voiceVolume).toInt();
    v.muteVoiceWhilePlaying = s.value("muteVoiceWhilePlaying", v.muteVoiceWhilePlaying).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("playback"));
    v.allowOverlap = s.value("allowOverlap", v.allowOverlap).toBool();
    v.hotkeyRestartsSound = s.value("hotkeyRestartsSound", v.hotkeyRestartsSound).toBool();
    v.loop = s.value("loop", v.loop).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("virtualMicrophone"));
    v.createVirtualMicrophone = s.value("create", v.createVirtualMicrophone).toBool();
    v.removeVirtualMicrophoneOnExit =
        s.value("removeOnExit", v.removeVirtualMicrophoneOnExit).toBool();
    s.endGroup();

    s.beginGroup(QStringLiteral("pushToTalk"));
    v.pushToTalkEnabled = s.value("enabled", v.pushToTalkEnabled).toBool();
    v.pushToTalkKey = s.value("key").toString();
    v.pushToTalkReleaseDelayMs = s.value("releaseDelayMs", v.pushToTalkReleaseDelayMs).toInt();
    s.endGroup();

    s.beginGroup(QStringLiteral("hotkeys"));
    v.stopHotkey = s.value("stop").toString();
    v.pauseHotkey = s.value("pause").toString();
    v.randomHotkey = s.value("random").toString();
    v.playSelectedHotkey = s.value("playSelected").toString();
    s.endGroup();

    s.beginGroup(QStringLiteral("ui"));
    v.closeToTray = s.value("closeToTray", v.closeToTray).toBool();
    v.startMinimized = s.value("startMinimized", v.startMinimized).toBool();
    v.windowsCableHintShown = s.value("windowsCableHintShown", false).toBool();
    v.recordingsFolder = s.value("recordingsFolder").toString();
    s.endGroup();
    return v;
}

void Settings::save() const
{
    QSettings s = openSettings();

    s.beginGroup(QStringLiteral("audio"));
    s.setValue("speakerDevice", speakerDevice);
    s.setValue("microphoneSinkDevice", microphoneSinkDevice);
    s.setValue("inputDevice", inputDevice);
    s.setValue("recordingDevice", recordingDevice);
    s.setValue("playToSpeakers", playToSpeakers);
    s.setValue("playToMicrophone", playToMicrophone);
    s.setValue("speakerVolume", speakerVolume);
    s.setValue("microphoneVolume", microphoneVolume);
    s.setValue("voicePassthrough", voicePassthrough);
    s.setValue("voiceVolume", voiceVolume);
    s.setValue("muteVoiceWhilePlaying", muteVoiceWhilePlaying);
    s.endGroup();

    s.beginGroup(QStringLiteral("playback"));
    s.setValue("allowOverlap", allowOverlap);
    s.setValue("hotkeyRestartsSound", hotkeyRestartsSound);
    s.setValue("loop", loop);
    s.endGroup();

    s.beginGroup(QStringLiteral("virtualMicrophone"));
    s.setValue("create", createVirtualMicrophone);
    s.setValue("removeOnExit", removeVirtualMicrophoneOnExit);
    s.endGroup();

    s.beginGroup(QStringLiteral("pushToTalk"));
    s.setValue("enabled", pushToTalkEnabled);
    s.setValue("key", pushToTalkKey);
    s.setValue("releaseDelayMs", pushToTalkReleaseDelayMs);
    s.endGroup();

    s.beginGroup(QStringLiteral("hotkeys"));
    s.setValue("stop", stopHotkey);
    s.setValue("pause", pauseHotkey);
    s.setValue("random", randomHotkey);
    s.setValue("playSelected", playSelectedHotkey);
    s.endGroup();

    s.beginGroup(QStringLiteral("ui"));
    s.setValue("closeToTray", closeToTray);
    s.setValue("startMinimized", startMinimized);
    s.setValue("windowsCableHintShown", windowsCableHintShown);
    s.setValue("recordingsFolder", recordingsFolder);
    s.endGroup();
}

} // namespace sb

#pragma once

#include <QString>
#include <QStringList>

// The "virtual microphone" is an output device the soundboard plays into whose
// audio other applications can record as if it were a microphone.
//
// Linux: a PulseAudio/PipeWire null sink plus a remapped source, created with
//        pactl.
// Windows: a virtual audio cable driver such as VB-CABLE must be installed;
//          the soundboard plays into its "CABLE Input" device.
namespace sb::virtualmic {

// True when the soundboard can create the virtual microphone itself.
bool canCreate();
// Name of the output device the soundboard writes to.
QString sinkDescription();
// Name of the microphone voice chat applications should select.
QString sourceDescription();

bool exists();
bool create(QString *error);
void remove();

// Capture devices that carry the soundboard's own output (our virtual mic,
// the recording side of a virtual cable). Using one of them as the voice
// input would create a feedback loop.
QStringList ownCaptureDevices(const QStringList &captureDevices);

// Picks a known virtual cable among the playback devices, or an empty string.
QString findCableDevice(const QStringList &playbackDevices);

} // namespace sb::virtualmic

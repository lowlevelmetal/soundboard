#include "VirtualMic.h"

#include <QCoreApplication>
#include <QProcess>
#include <QStandardPaths>

namespace sb::virtualmic {

namespace {

const QString kSinkName = QStringLiteral("soundboard_sink");
const QString kSourceName = QStringLiteral("soundboard_mic");

QString tr(const char *text)
{
    return QCoreApplication::translate("VirtualMic", text);
}

QString pactlPath()
{
#ifdef Q_OS_LINUX
    return QStandardPaths::findExecutable(QStringLiteral("pactl"));
#else
    return {};
#endif
}

bool runPactl(const QStringList &arguments, QString *output, QString *error = nullptr)
{
    const QString program = pactlPath();
    if (program.isEmpty()) {
        if (error)
            *error = tr("pactl was not found. Install the PulseAudio utilities "
                        "(pulseaudio-utils / libpulse).");
        return false;
    }
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(5000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        if (error) {
            const QString details = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
            *error = tr("pactl %1 failed: %2")
                         .arg(arguments.value(0),
                              details.isEmpty() ? process.errorString() : details);
        }
        return false;
    }
    if (output)
        *output = QString::fromLocal8Bit(process.readAllStandardOutput());
    return true;
}

// Second column of `pactl list short sinks|sources` is the node name.
bool listContains(const QString &kind, const QString &name)
{
    QString output;
    if (!runPactl({QStringLiteral("list"), QStringLiteral("short"), kind}, &output))
        return false;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.section(QLatin1Char('\t'), 1, 1) == name)
            return true;
    }
    return false;
}

bool sinkExists()
{
    return listContains(QStringLiteral("sinks"), kSinkName);
}

bool sourceExists()
{
    return listContains(QStringLiteral("sources"), kSourceName);
}

} // namespace

bool canCreate()
{
    return !pactlPath().isEmpty();
}

QString sinkDescription()
{
    return QStringLiteral("Soundboard Virtual Cable");
}

QString sourceDescription()
{
    return QStringLiteral("Soundboard Microphone");
}

bool exists()
{
    return canCreate() && sinkExists() && sourceExists();
}

bool create(QString *error)
{
    if (!canCreate()) {
        if (error)
            *error = tr("Creating a virtual microphone is not supported here.");
        return false;
    }
    // The quoting of the properties is understood by PulseAudio and PipeWire alike.
    if (!sinkExists()
        && !runPactl({QStringLiteral("load-module"), QStringLiteral("module-null-sink"),
                      QStringLiteral("sink_name=") + kSinkName, QStringLiteral("rate=48000"),
                      QStringLiteral("channels=2"),
                      QStringLiteral("channel_map=front-left,front-right"),
                      QStringLiteral("sink_properties='device.description=\"%1\"'")
                          .arg(sinkDescription())},
                     nullptr, error))
        return false;
    if (!sourceExists()
        && !runPactl({QStringLiteral("load-module"), QStringLiteral("module-remap-source"),
                      QStringLiteral("master=%1.monitor").arg(kSinkName),
                      QStringLiteral("source_name=") + kSourceName,
                      QStringLiteral("source_properties='device.description=\"%1\"'")
                          .arg(sourceDescription())},
                     nullptr, error))
        return false;
    return true;
}

void remove()
{
    QString output;
    if (!runPactl({QStringLiteral("list"), QStringLiteral("short"), QStringLiteral("modules")},
                  &output))
        return;
    // Unload the source before the sink it reads from.
    QStringList sourceModules;
    QStringList sinkModules;
    const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QString index = line.section(QLatin1Char('\t'), 0, 0);
        const QString arguments = line.section(QLatin1Char('\t'), 2);
        if (arguments.contains(QStringLiteral("source_name=") + kSourceName))
            sourceModules << index;
        else if (arguments.contains(QStringLiteral("sink_name=") + kSinkName))
            sinkModules << index;
    }
    for (const QString &index : std::as_const(sourceModules) + sinkModules)
        runPactl({QStringLiteral("unload-module"), index}, nullptr);
}

QStringList ownCaptureDevices(const QStringList &captureDevices)
{
    QStringList own = {sourceDescription(), QStringLiteral("Monitor of ") + sinkDescription()};
    static const QStringList cablePatterns = {
        QStringLiteral("CABLE Output"),
        QStringLiteral("VoiceMeeter Output"),
        QStringLiteral("VoiceMeeter Aux Output"),
    };
    for (const QString &device : captureDevices) {
        for (const QString &pattern : cablePatterns) {
            if (device.contains(pattern, Qt::CaseInsensitive) && !own.contains(device))
                own.append(device);
        }
    }
    return own;
}

QString findCableDevice(const QStringList &playbackDevices)
{
    // Known virtual cables, best match first. VB-CABLE's playback side is
    // "CABLE Input (VB-Audio Virtual Cable)".
    static const QStringList patterns = {
        sinkDescription(),
        QStringLiteral("CABLE Input"),
        QStringLiteral("VB-Audio Virtual Cable"),
        QStringLiteral("VoiceMeeter Aux Input"),
        QStringLiteral("VoiceMeeter Input"),
    };
    for (const QString &pattern : patterns) {
        for (const QString &device : playbackDevices) {
            if (device.contains(pattern, Qt::CaseInsensitive))
                return device;
        }
    }
    return {};
}

} // namespace sb::virtualmic

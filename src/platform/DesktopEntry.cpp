#include "DesktopEntry.h"

#include "ui/Icons.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>

namespace sb::desktop {

namespace {

const QByteArray kGeneratedMarker = "X-Soundboard-Generated=true";

// Quotes an Exec= argument as the Desktop Entry specification requires.
QString quoteExec(const QString &path)
{
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    bool needsQuotes = false;
    for (const QChar c : path)
        needsQuotes = needsQuotes || reserved.contains(c);
    if (!needsQuotes)
        return path;
    QString quoted;
    for (const QChar c : path) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$')
            || c == QLatin1Char('\\'))
            quoted += QLatin1Char('\\');
        quoted += c;
    }
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString executablePath()
{
    // Inside an AppImage the real, stable path is in $APPIMAGE.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

QString writeIcon(const QString &appId)
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1String("/icons/hicolor/256x256/apps");
    const QString path = directory + QLatin1Char('/') + appId + QLatin1String(".png");
    if (!QFile::exists(path)) {
        QDir().mkpath(directory);
        icons::appIconImage(256).save(path, "PNG");
    }
    return path;
}

} // namespace

bool ensureDesktopEntry(const QString &appId, QString *error)
{
    const QString fileName = appId + QLatin1String(".desktop");
    const QString exec = quoteExec(executablePath());

    const QString existing = QStandardPaths::locate(QStandardPaths::ApplicationsLocation, fileName);
    if (!existing.isEmpty()) {
        QFile file(existing);
        if (!file.open(QIODevice::ReadOnly))
            return true;
        const QByteArray contents = file.readAll();
        // Leave entries installed by a package or edited by the user alone.
        if (!contents.contains(kGeneratedMarker)
            || contents.contains(QByteArray("\nExec=") + exec.toUtf8() + '\n'))
            return true;
    }

    const QString directory = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    QDir().mkpath(directory);
    QSaveFile file(directory + QLatin1Char('/') + fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QByteArray contents;
    contents += "[Desktop Entry]\n";
    contents += "Type=Application\n";
    contents += "Name=Soundboard\n";
    contents += "Comment=Play sounds through your microphone\n";
    contents += "Exec=" + exec.toUtf8() + '\n';
    contents += "Icon=" + writeIcon(appId).toUtf8() + '\n';
    contents += "Terminal=false\n";
    contents += "Categories=AudioVideo;Audio;\n";
    contents += "Keywords=soundpad;sound;board;microphone;\n";
    contents += kGeneratedMarker + '\n';
    file.write(contents);
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace sb::desktop

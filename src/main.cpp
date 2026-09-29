#include "SingleInstance.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"

#include <QApplication>

#ifdef Q_OS_UNIX
#include <QSocketNotifier>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int g_signalPipe[2] = {-1, -1};

void handleSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const auto written = ::write(g_signalPipe[0], &byte, 1);
}

// Turns SIGTERM/SIGINT/SIGHUP (logout, kill, Ctrl+C) into a normal quit so the
// virtual microphone is removed and the sound list saved.
void quitOnTerminationSignals()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, g_signalPipe) != 0)
        return;
    auto *notifier = new QSocketNotifier(g_signalPipe[1], QSocketNotifier::Read, qApp);
    QObject::connect(notifier, &QSocketNotifier::activated, qApp, [] {
        char byte;
        [[maybe_unused]] const auto read = ::read(g_signalPipe[1], &byte, 1);
        QCoreApplication::quit();
    });
    struct sigaction action {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (int signal : {SIGTERM, SIGINT, SIGHUP})
        sigaction(signal, &action, nullptr);
}

} // namespace
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Soundboard"));
    QApplication::setApplicationVersion(QStringLiteral(SOUNDBOARD_VERSION));
    // On Wayland this becomes the window's app id and ties it to the desktop entry.
    QGuiApplication::setDesktopFileName(QStringLiteral(SOUNDBOARD_APP_ID));
    QApplication::setWindowIcon(sb::icons::appIcon());
    // The window may live in the tray, so closing dialogs must not end the app.
    QApplication::setQuitOnLastWindowClosed(false);
#ifdef Q_OS_UNIX
    quitOnTerminationSignals();
#endif

    sb::SingleInstance instance(QStringLiteral("soundboard"));
    if (!instance.claim())
        return 0;

    sb::MainWindow window;
    QObject::connect(&instance, &sb::SingleInstance::activationRequested, &window,
                     &sb::MainWindow::showAndRaise);
    if (!window.startsHidden())
        window.show();
    return app.exec();
}

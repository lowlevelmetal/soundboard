#pragma once

#include <QString>

namespace sb::desktop {

// Wayland desktop portals identify host applications by their desktop entry,
// so global shortcuts only work if "<appId>.desktop" is installed. This writes
// one to ~/.local/share/applications when no entry exists yet (or refreshes one
// we generated earlier if the executable moved).
bool ensureDesktopEntry(const QString &appId, QString *error);

} // namespace sb::desktop

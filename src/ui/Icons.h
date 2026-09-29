#pragma once

#include <QIcon>
#include <QImage>

// Icons are drawn in code so they look the same on every platform, follow the
// palette (light or dark) and need no image plugins.
namespace sb::icons {

enum class Glyph {
    Play,
    Pause,
    Stop,
    Previous,
    Next,
    Add,
    AddFolder,
    Remove,
    Record,
    Speaker,
    Microphone,
    Settings,
    Search,
    Loop,
    Shuffle,
    Folder,
    Tag,
    Edit,
    Keyboard,
    Warning,
    Info,
    Quit,
};

QIcon icon(Glyph glyph);
// For checkable buttons: the unchecked (Off) state is drawn struck through.
QIcon toggleIcon(Glyph glyph);
QIcon appIcon();
QImage appIconImage(int size);

} // namespace sb::icons

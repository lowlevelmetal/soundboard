# Soundboard

A Soundpad-style soundboard for **Linux and Windows**, written in C++ with Qt 6.
It plays sounds into a virtual microphone so the people you talk to in Discord,
TeamSpeak, OBS or games hear them, and on your own speakers at the same time.
Your real microphone is mixed in, so you can keep talking.

```
 sound files ──┬──────────────────────────────▶ speakers / headphones
               │
               └──▶ ┌──────────────┐
 your microphone ──▶│  Soundboard  │──▶ virtual microphone ──▶ Discord, games…
                    └──────────────┘
```

## Features

- Sound list with play count, duration, date added and file path; search,
  sort, drag to reorder, drop files or folders from the file manager to add them
- Categories (drag sounds onto a category to move them)
- A global hotkey for every sound, plus global hotkeys for stop, pause/resume,
  random sound and "play selected"
- Separate volume and on/off for speakers, microphone output and your own voice,
  with level meters
- Per-sound volume (0–200 %), loop, pause, seek, previous/next, random
- Optional overlapping playback; a sound's hotkey can restart or stop it
- Push-to-talk: holds a key down while a sound plays
- Record new sounds from any input (or, on Windows, from what the speakers play)
- Formats: WAV, MP3, FLAC, Ogg Vorbis
- System tray icon (where the desktop has a tray), single instance, light and
  dark themes

## How the virtual microphone works

**Linux:** Soundboard creates it for you. On start it loads two PulseAudio
modules through `pactl` (this works with PipeWire's `pipewire-pulse` too):
a null sink called *Soundboard Virtual Cable* that it plays into, and a
microphone called **Soundboard Microphone** that records from it. Pick
*Soundboard Microphone* as the input device in your voice chat app. The modules
are removed when Soundboard quits (this can be turned off in Settings → Audio).

**Windows:** Windows can't create audio devices without a driver, so install
the free [VB-CABLE](https://vb-audio.com/Cable/) virtual audio cable first.
Soundboard finds **CABLE Input** automatically and plays into it; select
**CABLE Output** as the microphone in your voice chat app.
VoiceMeeter's inputs are also detected.

In both cases *Help → Microphone Setup* shows the steps and the current state.

## Global hotkeys

| Platform | Mechanism | Notes |
|---|---|---|
| Windows | `RegisterHotKey` | A hotkey another program already registered can't be used. |
| Linux, Wayland | XDG desktop portal `GlobalShortcuts` | Tested on GNOME 50 (GNOME 48+ has the portal); KDE Plasma and Hyprland implement it too. The desktop asks you to approve new hotkeys once. |
| Linux, X11 | `XGrabKey` | |

Wayland portals identify apps by their desktop entry, so on Wayland Soundboard
writes `~/.local/share/applications/io.github.soundboard.Soundboard.desktop`
(and an icon) the first time it runs, unless an entry is already installed.
Compositors without the GlobalShortcuts portal (for example sway) can't offer
global hotkeys to applications.

Hotkeys that couldn't be registered get a warning icon in the Hotkey column;
hover it for the reason.

## Building

Requirements: a C++20 compiler, CMake 3.21+, Qt 6.4 or newer (Widgets, Network,
and on Linux DBus). miniaudio is included in `third_party/`.

### Linux

```sh
# Arch
sudo pacman -S --needed base-devel cmake ninja qt6-base libx11 libxcb
# Debian / Ubuntu (24.04+)
sudo apt install build-essential cmake ninja-build qt6-base-dev libx11-dev libxcb1-dev pulseaudio-utils
# Fedora
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel libX11-devel libxcb-devel pulseaudio-utils

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/soundboard
```

`sudo cmake --install build` installs the binary, a desktop entry and an icon.
At runtime `pactl` must be available (package `libpulse` on Arch,
`pulseaudio-utils` on Debian/Ubuntu/Fedora).

### Windows

1. Install Visual Studio 2022 (Desktop development with C++), CMake, and Qt 6
   for MSVC 2022 64-bit from the Qt online installer.
2. In a *Developer PowerShell for VS 2022*:

   ```powershell
   cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
   cmake --build build --config Release
   cmake --install build --config Release --prefix dist
   ```

   `dist\bin` then contains `soundboard.exe` together with the Qt DLLs
   (`windeployqt` runs as part of the install step).
3. Install [VB-CABLE](https://vb-audio.com/Cable/) and restart Soundboard.

`.github/workflows/build.yml` builds both platforms on GitHub Actions and
uploads the packaged Windows build as an artifact.

## Push-to-talk

If your voice chat uses push-to-talk, enable Settings → Push-to-Talk and enter
the same key. On Windows the key is sent with `SendInput`. On Linux it is sent
through a virtual keyboard on `/dev/uinput`, which your user must be allowed to
write. Some distributions (Arch, for example) already allow that for the
logged-in user; otherwise add a udev rule:

```sh
echo 'KERNEL=="uinput", SUBSYSTEM=="misc", TAG+="uaccess", OPTIONS+="static_node=uinput"' \
  | sudo tee /etc/udev/rules.d/60-soundboard-uinput.rules
sudo udevadm control --reload && sudo udevadm trigger
```

Apps running under XWayland (Discord included) only see key presses while they
have focus, so on Wayland voice activation works better than push-to-talk.

## Keyboard shortcuts in the window

| Key | Action |
|---|---|
| Enter / double-click | Play the selected sound |
| Space | Pause / resume |
| Esc | Stop all sounds |
| Ctrl+← / Ctrl+→ | Previous / next sound |
| Ctrl+Shift+R | Random sound |
| Ctrl+L | Loop |
| Ctrl+K | Set hotkey |
| F2 | Rename |
| Alt+Enter | Properties |
| Delete | Remove from the list (files are not deleted) |
| Ctrl+F | Search |
| Ctrl+O / Ctrl+Shift+O | Add sounds / add folder |
| Ctrl+R | Start / stop recording |

## Where things are stored

| | Linux | Windows |
|---|---|---|
| Settings | `~/.config/Soundboard/settings.ini` | `%LOCALAPPDATA%\Soundboard\settings.ini` |
| Sound list | `~/.local/share/Soundboard/library.json` | `%APPDATA%\Soundboard\library.json` |
| Recordings | `~/Music/Soundboard Recordings` | `Music\Soundboard Recordings` |

The sound list only references your files; nothing is copied.

## Troubleshooting

- **Nobody hears the sounds:** check the *Microphone* meter at the bottom of the
  window moves when a sound plays, and that the voice chat app uses
  *Soundboard Microphone* (Linux) or *CABLE Output* (Windows).
- **"Audio problem" in the status bar:** click it for details; *Try Again*
  reopens the devices (for example after plugging a headset back in).
- **Hotkeys on Wayland:** run with
  `QT_LOGGING_RULES="soundboard.hotkeys.debug=true" soundboard` to see the
  conversation with the desktop portal. GNOME remembers approved shortcuts in
  dconf under `/org/gnome/settings-daemon/global-shortcuts/`.

## Code layout

```
src/audio/     AudioEngine: miniaudio devices, mixing, voice passthrough, recording
src/library/   SoundLibrary (sound list model + JSON storage), search/category filter
src/hotkeys/   HotkeyManager and the Windows, X11 and Wayland portal backends
src/platform/  Virtual microphone (pactl), push-to-talk key presser, desktop entry
src/ui/        Main window, dialogs, drawn icons and small widgets
```

Third-party code: [miniaudio](https://miniaud.io) 0.11.25 (public domain /
MIT-0, see `third_party/miniaudio/LICENSE`), including stb_vorbis. Qt is used
under the LGPL.

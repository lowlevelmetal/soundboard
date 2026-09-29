cmake_minimum_required(VERSION 3.21)

project(soundboard
    VERSION 0.1.0
    DESCRIPTION "Play sounds through your microphone and speakers"
    LANGUAGES C CXX)

set(SOUNDBOARD_APP_ID "io.github.soundboard.Soundboard" CACHE STRING
    "Reverse-DNS application id, used for the desktop entry and Wayland global shortcuts")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SOUNDBOARD_LINUX ON)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)
if(SOUNDBOARD_LINUX)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()
find_package(Threads REQUIRED)

qt_standard_project_setup()

# ---------------------------------------------------------------------------
# miniaudio (vendored single-header library: playback, capture, decoding)
# ---------------------------------------------------------------------------
add_library(miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(miniaudio SYSTEM PUBLIC third_party/miniaudio)
target_compile_definitions(miniaudio PUBLIC
    MA_NO_ENGINE
    MA_NO_NODE_GRAPH
    MA_NO_RESOURCE_MANAGER
    MA_NO_GENERATION)
target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(miniaudio PUBLIC m)
endif()
if(MSVC)
    target_compile_options(miniaudio PRIVATE /W0)
else()
    target_compile_options(miniaudio PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
qt_add_executable(soundboard WIN32
    src/main.cpp
    src/Settings.cpp
    src/Settings.h
    src/SingleInstance.cpp
    src/SingleInstance.h
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/hotkeys/HotkeyBackend.h
    src/hotkeys/HotkeyManager.cpp
    src/hotkeys/HotkeyManager.h
    src/hotkeys/KeyMapping.cpp
    src/hotkeys/KeyMapping.h
    src/library/SoundFilterModel.cpp
    src/library/SoundFilterModel.h
    src/library/SoundLibrary.cpp
    src/library/SoundLibrary.h
    src/platform/KeyPresser.cpp
    src/platform/KeyPresser.h
    src/platform/VirtualMic.cpp
    src/platform/VirtualMic.h
    src/ui/CategoryList.cpp
    src/ui/CategoryList.h
    src/ui/Icons.cpp
    src/ui/Icons.h
    src/ui/KeyCaptureEdit.cpp
    src/ui/KeyCaptureEdit.h
    src/ui/LevelMeter.cpp
    src/ui/LevelMeter.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/SeekSlider.cpp
    src/ui/SeekSlider.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SoundPropertiesDialog.cpp
    src/ui/SoundPropertiesDialog.h
    src/ui/SoundTableView.cpp
    src/ui/SoundTableView.h
)

target_include_directories(soundboard PRIVATE src)
target_compile_definitions(soundboard PRIVATE
    SOUNDBOARD_VERSION="${PROJECT_VERSION}"
    SOUNDBOARD_APP_ID="${SOUNDBOARD_APP_ID}")
target_link_libraries(soundboard PRIVATE Qt6::Widgets Qt6::Network miniaudio)

if(MSVC)
    target_compile_options(soundboard PRIVATE /W4 /utf-8)
else()
    target_compile_options(soundboard PRIVATE -Wall -Wextra)
endif()

if(SOUNDBOARD_LINUX)
    target_sources(soundboard PRIVATE
        src/hotkeys/PortalHotkeyBackend.cpp
        src/hotkeys/PortalHotkeyBackend.h
        src/platform/DesktopEntry.cpp
        src/platform/DesktopEntry.h)
    target_link_libraries(soundboard PRIVATE Qt6::DBus)

    find_package(X11)
    if(X11_FOUND AND X11_xcb_FOUND)
        target_sources(soundboard PRIVATE
            src/hotkeys/X11HotkeyBackend.cpp
            src/hotkeys/X11HotkeyBackend.h)
        target_link_libraries(soundboard PRIVATE X11::X11 X11::xcb)
        target_compile_definitions(soundboard PRIVATE SOUNDBOARD_HAVE_X11)
    else()
        message(STATUS "X11/xcb not found: global hotkeys on X11 sessions are disabled")
    endif()
elseif(WIN32)
    target_sources(soundboard PRIVATE
        src/hotkeys/WindowsHotkeyBackend.cpp
        src/hotkeys/WindowsHotkeyBackend.h)
    target_link_libraries(soundboard PRIVATE user32)
    target_compile_definitions(soundboard PRIVATE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS soundboard
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .)

if(SOUNDBOARD_LINUX)
    set(SOUNDBOARD_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/soundboard")
    configure_file(packaging/soundboard.desktop.in
        ${CMAKE_CURRENT_BINARY_DIR}/${SOUNDBOARD_APP_ID}.desktop @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${SOUNDBOARD_APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/soundboard.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME ${SOUNDBOARD_APP_ID}.svg)
endif()

if(WIN32)
    # Copies the Qt runtime next to the installed executable (runs windeployqt).
    qt_generate_deploy_app_script(
        TARGET soundboard
        OUTPUT_SCRIPT soundboard_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${soundboard_deploy_script})
endif()

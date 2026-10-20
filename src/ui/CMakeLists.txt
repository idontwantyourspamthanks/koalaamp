# SPDX-License-Identifier: MIT
# ui — the only Qt-Widgets-dependent layer (docs/spec.md §2.2).
#
# SE-2 (docs/tickets/SE-2-main-window.md) turns the M0 stub into the classic
# main window: the sprite renderer, the integer-scale canvas, the region.txt
# framed toplevel and the control wiring over the playback transport seam.

add_library(koalaamp_ui STATIC
    Atf.cpp
    Atf.hpp
    DspEqSink.hpp
    EqGoldenStates.cpp
    EqGoldenStates.hpp
    EqHitRegion.cpp
    EqHitRegion.hpp
    EqRenderer.cpp
    EqRenderer.hpp
    EqSink.hpp
    EqState.hpp
    EqWindow.cpp
    EqWindow.hpp
    GoldenStates.cpp
    GoldenStates.hpp
    JumpToFileDialog.cpp
    JumpToFileDialog.hpp
    LibraryWindow.cpp
    LibraryWindow.hpp
    MainWindow.cpp
    MainWindow.hpp
    MediaName.cpp
    MediaName.hpp
    PcmFanout.hpp
    PresetCycler.cpp
    PresetCycler.hpp
    PresetSoak.cpp
    PresetSoak.hpp
    PresetStore.cpp
    PresetStore.hpp
    PlayerRenderer.cpp
    PlayerRenderer.hpp
    PlayerState.hpp
    PleditCanvas.cpp
    PleditCanvas.hpp
    PleditGoldens.cpp
    PleditGoldens.hpp
    PleditHitRegion.cpp
    PleditHitRegion.hpp
    PleditRenderer.cpp
    PleditRenderer.hpp
    PleditState.cpp
    PleditState.hpp
    PlaylistWindow.cpp
    PlaylistWindow.hpp
    SkinCanvas.cpp
    SkinCanvas.hpp
    SkinHitRegion.cpp
    SkinHitRegion.hpp
    SkinnedWindow.cpp
    SkinnedWindow.hpp
    StereoPcmRing.cpp
    StereoPcmRing.hpp
    TitleText.cpp
    TitleText.hpp
    Transport.hpp
    VisData.cpp
    VisData.hpp
    VisFeed.cpp
    VisFeed.hpp
    WindowPlatform.cpp
    WindowPlatform.hpp
    WindowSet.cpp
    WindowSet.hpp)

koalaamp_enable_warnings(koalaamp_ui)

# Public headers are addressed as "ui/<Header>.hpp" from the app and tests.
target_include_directories(koalaamp_ui
    PUBLIC "${CMAKE_SOURCE_DIR}/src")

# Widgets/Gui are PUBLIC: MainWindow.hpp exposes QWidget, so consumers (the app
# and widget tests) need the Qt include paths transitively. koalaamp_playback
# is the header-only playback seam (IAudioEngine/IPlaybackObserver) — it never
# pulls libvlc into a consumer's link line.
target_link_libraries(koalaamp_ui
    PUBLIC koalaamp_library koalaamp_skins koalaamp_playback koalaamp_dsp koalaamp_playlist
           Qt6::Widgets Qt6::Gui
    PRIVATE Qt6::OpenGL)

# --- VZ-1: the visualizer's GL canvas ---------------------------------------
# Qt's OpenGLWidgets module is found *softly*: a Qt built without the OpenGL feature
# must still configure, with the visualizer simply absent (main.cpp's wiring is
# compiled out via KOALAAMP_HAS_GL_VISUALIZER) rather than failing on a machine we
# cannot reproduce. The engine faade is linked PUBLIC because the canvas exposes its
# types; without libprojectM the faade supplies its stub, so this is always safe.
find_package(Qt6 QUIET COMPONENTS OpenGLWidgets)
if(TARGET Qt6::OpenGLWidgets)
    target_sources(koalaamp_ui PRIVATE
        GenRenderer.cpp
        GenRenderer.hpp
        VisualizerCanvas.cpp
        VisualizerCanvas.hpp
        VisualizerOverlay.cpp
        VisualizerOverlay.hpp
        VisualizerWindow.cpp
        MigrationDialog.cpp
        MigrationDialog.hpp
        ConnectionDialog.cpp
        ConnectionDialog.hpp
        JellyfinBrowserSource.cpp
        JellyfinBrowserSource.hpp
        PlexBrowserSource.cpp
        PlexBrowserSource.hpp
        ServerBrowserWindow.cpp
        ServerBrowserWindow.hpp
        ServerBrowserSource.hpp
        VisualizerWindow.hpp)
    # koalaamp_interop is only for the seam header's include path: the canvas takes
    # its engine from the caller, so this module never links the rendering back end
    # (and its third-party headers stay out of every UI consumer).
    # The connection dialog lives in this block because it paints the same gen chrome
    # (`GenRenderer`). It is the one UI file that needs the Jellyfin gateway, which
    # brings curl through its own target.
    target_link_libraries(koalaamp_ui PUBLIC Qt6::OpenGLWidgets
                          PRIVATE koalaamp_interop koalaamp_jellyfin koalaamp_servers
                                  koalaamp_plex koalaamp_migration)
    target_compile_definitions(koalaamp_ui PUBLIC KOALAAMP_HAS_GL_VISUALIZER=1)
    set(KOALAAMP_HAS_GL_VISUALIZER TRUE CACHE INTERNAL "Qt OpenGLWidgets present (VZ-1)")
    message(STATUS "KoalaAmp: Qt OpenGLWidgets found - the visualizer canvas is enabled")
else()
    set(KOALAAMP_HAS_GL_VISUALIZER FALSE CACHE INTERNAL "Qt OpenGLWidgets present (VZ-1)")
    message(STATUS "KoalaAmp: Qt OpenGLWidgets missing - the visualizer window is disabled")
endif()

# The PlaybackCore bridge is a separate target because it (and only it) needs
# the libvlc-backed core; koalaamp_ui itself stays engine-free so the widget
# and renderer tests run anywhere Qt does.
if(TARGET koalaamp_playback_core)
    add_library(koalaamp_ui_playback STATIC
        PlaybackCoreTransport.cpp
        PlaybackCoreTransport.hpp)
    koalaamp_enable_warnings(koalaamp_ui_playback)
    target_include_directories(koalaamp_ui_playback
        PUBLIC "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(koalaamp_ui_playback
        PUBLIC koalaamp_ui
        PRIVATE koalaamp_playback_core)
    message(STATUS "KoalaAmp: SE-2 PlaybackCore transport enabled")
endif()

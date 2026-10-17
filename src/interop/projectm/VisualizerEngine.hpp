// SPDX-License-Identifier: MIT
//
// IVisualizerEngine — the seam between the visualizer window and whatever renders
// MilkDrop presets for it (docs/research/visuals.md §6.2; docs/spec.md §6.1-6.2).
// The window never calls libprojectM itself, so a parity engine (Butterchurn) or a
// Metal renderer — macOS eventually drops OpenGL (docs/feasibility.md §5) — can
// slot in behind this without touching window code.
//
// The lifecycle is GL-bound, and FE-14 measured why (docs/spikes/projectm-qt.md):
//   * create(), destroy() and loadPreset*() must run with the window's GL context
//     current. A preset loaded from a timer callback (no current context) gave
//     GL_INVALID_OPERATION and, after a handful of switches, permanent black
//     output — hence the window queues loads into paintGL.
//   * render() draws into the FBO the widget hands it (QOpenGLWidget's
//     defaultFramebufferObject()). projectM 4.1.8 hardcodes framebuffer 0 and
//     cannot do this at all, which is why the dependency is pinned to 4.2.0-dev.
//
// Sizes here are *device* pixels: projectM renders in device pixels, so the window
// multiplies its logical size by devicePixelRatioF() before calling in.
//
// Nothing below allocates per frame: pushPcm runs in the render loop and keeps its
// scratch internally.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace koalaamp::interop::visualizer {

class IVisualizerEngine {
public:
    virtual ~IVisualizerEngine() = default;

    /// False for the stub this builds on a machine without libprojectM: the
    /// window then reports the visualizer as unavailable instead of failing.
    virtual bool available() const = 0;

    /// Bring the engine up for a device-pixel viewport. Requires a current GL
    /// context; returns false (with the reason in `lastError()`) when there is
    /// none or projectM refuses to start. Safe to call again after destroy().
    virtual bool create(int widthPx, int heightPx) = 0;
    /// Tear the engine down. Requires a current GL context — a window can be
    /// destroyed after its context has gone, so the widget calls this from the
    /// context's aboutToBeDestroyed rather than from the window's destructor.
    virtual void destroy() = 0;
    /// New device-pixel size for the running engine.
    virtual void resize(int widthPx, int heightPx) = 0;

    /// Load a preset. Must be called with the GL context current: queue it into
    /// the widget's paintGL, never call it from a timer callback.
    ///
    /// A true return means the request was accepted, NOT that the preset loaded:
    /// projectM's load calls return void and report failure asynchronously through
    /// the switch-failed callback, which lastError() surfaces.
    virtual bool loadPresetFile(const std::string& path, bool smoothTransition) = 0;
    virtual bool loadPresetData(const std::string& name, const std::string& data,
                                bool smoothTransition) = 0;

    /// Feed interleaved stereo PCM (2 floats per frame) from the visualizer's
    /// ring. Never blocks, never allocates: frames beyond projectM's per-call
    /// maximum are chunked internally.
    ///
    /// Render thread only — the canvas drains its ring in paintGL. Never call this
    /// from the audio callback: the engine owns buffers and state that have no
    /// business on that thread (PcmFanout's "no allocation, no lock" rule).
    virtual void pushPcm(const float* interleavedStereo, std::size_t frames) = 0;

    /// Render one frame into `targetFbo`.
    virtual void render(std::uint32_t targetFbo) = 0;

    /// The preset we asked for — tracked here, because the vendored build disables
    /// the projectM playlist API that would report the current one.
    virtual std::string presetName() const = 0;

    /// The last warning/error projectM reported, or why create() failed.
    virtual std::string lastError() const = 0;
};

/// The engine for this build: a real renderer when the façade compiled against
/// libprojectM, otherwise a stub whose available() is false. Never null, so call
/// sites never need a null check.
std::unique_ptr<IVisualizerEngine> createEngine();

/// Compile-time availability — true when createEngine() returns a renderer.
bool engineAvailable();

}  // namespace koalaamp::interop::visualizer

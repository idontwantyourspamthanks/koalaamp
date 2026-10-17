// SPDX-License-Identifier: MIT
//
// The projectM implementation of IVisualizerEngine, plus the stub this builds when
// libprojectM is absent. The lifecycle rules in the header come from FE-14's
// measurements (docs/spikes/projectm-qt.md), not from taste — in particular the
// current-context requirements and the FBO render path.
#include "interop/projectm/VisualizerEngine.hpp"

#ifdef KOALAAMP_HAS_PROJECTM

#include <projectM-4/projectM.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <utility>
#include <vector>

namespace koalaamp::interop::visualizer {
namespace {

/// projectM's callbacks write here — they receive this through user_data, so the
/// engine needs no friendship. lastError() reports the last warning-or-worse.
struct CallbackState {
    // projectM may invoke the callbacks from threads other than the one that set
    // them (current_thread_only is false, so a load on the GL thread still reports
    // worker-thread logs), so the status string needs a guard. pushPcm never comes
    // near this, which keeps the render path lock-free and allocation-free.
    mutable std::mutex mutex;
    std::string lastError;
};

extern "C" void projectmLogCallback(const char* message, projectm_log_level level,
                                    void* userData) {
    auto* state = static_cast<CallbackState*>(userData);
    if (state == nullptr || message == nullptr) {
        return;
    }
    if (level >= PROJECTM_LOG_LEVEL_WARN) {
        const std::scoped_lock lock(state->mutex);
        state->lastError = message;
    }
}

extern "C" void projectmPresetFailedCallback(const char* filename, const char* message,
                                             void* userData) {
    auto* state = static_cast<CallbackState*>(userData);
    if (state == nullptr) {
        return;
    }
    std::string text = "preset switch failed: ";
    text += filename != nullptr ? filename : "(unknown preset)";
    if (message != nullptr) {
        text += ": ";
        text += message;
    }
    {
        const std::scoped_lock lock(state->mutex);
        state->lastError = std::move(text);
    }
}

/// The host drives preset cycling (VZ-1b), so projectM's own advance is not used.
/// It is set explicitly rather than left at the default because a small default
/// would blank a long-running visualizer mid-session — which reads as a renderer
/// bug while being nothing of the sort.
constexpr double kProjectMPresetDurationSeconds = 3600.0;

class ProjectMEngine final : public IVisualizerEngine {
public:
    ProjectMEngine() = default;

    ~ProjectMEngine() override {
        if (handle_ != nullptr) {
            // Past the widget's teardown the GL context is gone and
            // projectm_destroy() touches GL. Leaking one handle at process exit is
            // the lesser evil; the window is required to call destroy() from its
            // context's aboutToBeDestroyed.
            std::fprintf(stderr,
                         "visualizer: engine released without GL teardown "
                         "(leaking the projectM handle)\n");
        }
    }

    bool available() const override { return true; }

    bool create(int widthPx, int heightPx) override {
        destroy();
        handle_ = projectm_create();
        if (handle_ == nullptr) {
            state_.lastError = "projectm_create() failed (no current OpenGL context?)";
            return false;
        }
        projectm_set_log_callback(&projectmLogCallback, false, &state_);
        projectm_set_log_level(PROJECTM_LOG_LEVEL_WARN, false);
        resize(widthPx, heightPx);
        projectm_set_mesh_size(handle_, 48, 32);
        projectm_set_fps(handle_, 60);
        projectm_set_aspect_correction(handle_, true);
        projectm_set_beat_sensitivity(handle_, 1.0F);
        projectm_set_preset_duration(handle_, kProjectMPresetDurationSeconds);
        projectm_set_preset_switch_failed_event_callback(handle_, &projectmPresetFailedCallback,
                                                         &state_);
        scratch_.assign(static_cast<std::size_t>(projectm_pcm_get_max_samples()) * 2, 0.0F);
        presetName_.clear();
        return true;
    }

    void destroy() override {
        if (handle_ == nullptr) {
            return;
        }
        projectm_destroy(handle_);
        handle_ = nullptr;
        scratch_.clear();
        presetName_.clear();
    }

    void resize(int widthPx, int heightPx) override {
        if (handle_ == nullptr) {
            return;
        }
        const auto width = static_cast<std::size_t>(std::max(1, widthPx));
        const auto height = static_cast<std::size_t>(std::max(1, heightPx));
        projectm_set_window_size(handle_, width, height);
    }

    bool loadPresetFile(const std::string& path, bool smoothTransition) override {
        if (handle_ == nullptr) {
            return false;
        }
        projectm_load_preset_file(handle_, path.c_str(), smoothTransition);
        presetName_ = std::filesystem::path(path).filename().string();
        return true;
    }

    bool loadPresetData(const std::string& name, const std::string& data,
                        bool smoothTransition) override {
        if (handle_ == nullptr) {
            return false;
        }
        projectm_load_preset_data(handle_, data.c_str(), smoothTransition);
        presetName_ = name;
        return true;
    }

    void pushPcm(const float* interleavedStereo, std::size_t frames) override {
        if (handle_ == nullptr || interleavedStereo == nullptr || frames == 0) {
            return;
        }
        // projectM accepts at most projectm_pcm_get_max_samples() frames per call.
        // The scratch was sized once in create(), so the render loop never
        // allocates.
        const std::size_t maxFrames = scratch_.size() / 2;
        if (maxFrames == 0) {
            return;
        }
        std::size_t offset = 0;
        while (offset < frames) {
            const std::size_t chunk = std::min(maxFrames, frames - offset);
            std::copy_n(interleavedStereo + offset * 2, chunk * 2, scratch_.begin());
            projectm_pcm_add_float(handle_, scratch_.data(), static_cast<unsigned int>(chunk),
                                   PROJECTM_STEREO);
            offset += chunk;
        }
    }

    void render(std::uint32_t targetFbo) override {
        if (handle_ == nullptr) {
            return;
        }
        projectm_opengl_render_frame_fbo(handle_, targetFbo);
    }

    std::string presetName() const override { return presetName_; }
    std::string lastError() const override {
        const std::scoped_lock lock(state_.mutex);
        return state_.lastError;
    }

private:
    projectm_handle handle_ = nullptr;
    CallbackState state_;
    std::vector<float> scratch_;
    std::string presetName_;
};

}  // namespace

std::unique_ptr<IVisualizerEngine> createEngine() {
    return std::make_unique<ProjectMEngine>();
}

bool engineAvailable() { return true; }

}  // namespace koalaamp::interop::visualizer

#else  // !KOALAAMP_HAS_PROJECTM

namespace koalaamp::interop::visualizer {
namespace {

/// Built without libprojectM (vendor-deps not run, or a build job with no GL
/// headers): every call is a safe no-op and the window reports the reason.
class StubVisualizerEngine final : public IVisualizerEngine {
public:
    bool available() const override { return false; }

    bool create(int, int) override {
        lastError_ = "this build has no libprojectM (run scripts/vendor-deps.sh)";
        return false;
    }
    void destroy() override {}
    void resize(int, int) override {}
    bool loadPresetFile(const std::string&, bool) override { return false; }
    bool loadPresetData(const std::string&, const std::string&, bool) override { return false; }
    void pushPcm(const float*, std::size_t) override {}
    void render(std::uint32_t) override {}
    std::string presetName() const override { return {}; }
    std::string lastError() const override { return lastError_; }

private:
    std::string lastError_;
};

}  // namespace

std::unique_ptr<IVisualizerEngine> createEngine() {
    return std::make_unique<StubVisualizerEngine>();
}

bool engineAvailable() { return false; }

}  // namespace koalaamp::interop::visualizer

#endif  // KOALAAMP_HAS_PROJECTM

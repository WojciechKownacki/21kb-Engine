#pragma once

#include "profiler/ProfilerOverlayModel.hpp"

#include <chrono>

namespace kb::editor {

// The editor's profiler: the measured values, what the scene shows of them and whether the window
// that chooses them is open. The numbers on the scene change at the pace the options ask for
// instead of at the pace of the frames, so they stay readable.
class EditorProfilerState final {
public:
    [[nodiscard]] kb::core::ProfilerRegistry& Registry() noexcept { return registry_; }
    [[nodiscard]] const kb::core::ProfilerRegistry& Registry() const noexcept { return registry_; }

    [[nodiscard]] bool WindowOpen() const noexcept { return windowOpen_; }
    void SetWindowOpen(bool open) noexcept { windowOpen_ = open; }

    [[nodiscard]] const ProfilerOverlayModel& Overlay() const noexcept { return overlay_; }

    // Rebuilds the scene's model when the refresh interval has passed, or at once when asked to.
    void RefreshOverlay(const ProfilerSelection& selection,
        std::chrono::steady_clock::time_point now, bool force = false) {
        const std::uint8_t rate = selection.overlay.refreshPerSecond;
        const bool due = rate == 0U || now - lastRefresh_ >= std::chrono::milliseconds{ 1000 / rate };
        if (!force && !due) return;
        overlay_ = BuildProfilerOverlayModel(registry_, selection);
        lastRefresh_ = now;
    }

private:
    kb::core::ProfilerRegistry registry_;
    ProfilerOverlayModel overlay_;
    std::chrono::steady_clock::time_point lastRefresh_{};
    bool windowOpen_ = false;
};

} // namespace kb::editor

#pragma once

#include "engine/core/ProfilerCounters.hpp"
#include "engine/core/ProfilerDiagnosis.hpp"
#include "engine/core/ProfilerFrameRegions.hpp"
#include "engine/core/ProfilerRecorder.hpp"
#include "profiler/ProfilerOverlayModel.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kb::editor {

enum class ProfilerPanelTab : std::uint8_t { Measurements, Counters, Diagnosis };

// What the Profiler panel is showing and where it is scrolled.
struct ProfilerPanelState {
    ProfilerPanelTab tab = ProfilerPanelTab::Measurements;
    // Paused: no frame is sampled, so what is on screen stays.
    bool paused = false;
    // The frame the hierarchy is of; empty follows the latest one.
    std::optional<std::uint64_t> selectedFrame;
    int leftScroll = 0;
    int hierarchyScroll = 0;
    int countersScroll = 0;
    int findingsScroll = 0;
    // The finding the details show: its position among the findings that are listed.
    int selectedFinding = -1;
    std::array<bool, 3> showSeverity{ true, true, true };
    std::array<bool, 4> showCategory{ true, true, true, true };
    bool showIgnored = false;
    // "rule/subject" of findings the user chose to ignore, until the next session.
    std::set<std::string> ignored;
    // Where the last recording or snapshot went, or why it did not.
    std::string lastSaved;
};

// The editor's profiler: the measured values, the regions of each frame, the recording, what the
// diagnosis found, what the scene shows of them and what the panel and its window are doing. The
// numbers on the scene change at the pace the options ask for instead of at the pace of the
// frames, so they stay readable.
class EditorProfilerState final {
public:
    [[nodiscard]] kb::core::ProfilerRegistry& Registry() noexcept { return registry_; }
    [[nodiscard]] const kb::core::ProfilerRegistry& Registry() const noexcept { return registry_; }
    // The timeline of the frame being measured; the sampler fills it and files it in FrameRegions().
    [[nodiscard]] kb::core::ProfilerCounters& Regions() noexcept { return regions_; }
    [[nodiscard]] kb::core::ProfilerFrameRegions& FrameRegions() noexcept { return frameRegions_; }
    [[nodiscard]] const kb::core::ProfilerFrameRegions& FrameRegions() const noexcept { return frameRegions_; }
    [[nodiscard]] kb::core::ProfilerRecorder& Recorder() noexcept { return recorder_; }
    [[nodiscard]] const kb::core::ProfilerRecorder& Recorder() const noexcept { return recorder_; }
    [[nodiscard]] const std::vector<kb::core::DiagnosisFinding>& Findings() const noexcept { return findings_; }
    [[nodiscard]] ProfilerPanelState& Panel() noexcept { return panel_; }
    [[nodiscard]] const ProfilerPanelState& Panel() const noexcept { return panel_; }

    [[nodiscard]] bool WindowOpen() const noexcept { return windowOpen_; }
    void SetWindowOpen(bool open) noexcept { windowOpen_ = open; }

    [[nodiscard]] const ProfilerOverlayModel& Overlay() const noexcept { return overlay_; }

    // The number of the frame last sampled; the first is 1.
    [[nodiscard]] std::uint64_t Frame() const noexcept { return frame_; }
    std::uint64_t NextFrame() noexcept { return ++frame_; }

    // The thresholds of the selection become the registry's, so everything that colors a reading
    // or measures it against a budget agrees with what the user set.
    void SyncThresholds(const ProfilerSelection& selection) {
        if (synced_ && selection.thresholds == syncedThresholds_) return;
        for (std::size_t counter = 0U; counter < registry_.CounterCount(); ++counter) registry_.ResetThresholds(counter);
        for (const ProfilerThreshold& threshold : selection.thresholds) {
            if (const std::optional<std::size_t> counter = registry_.Find(threshold.counter)) registry_.SetThresholds(*counter, threshold.budget, threshold.alarm);
        }
        syncedThresholds_ = selection.thresholds;
        synced_ = true;
    }

    // Runs the diagnosis over what has been measured; the panel's list of findings is its result.
    void Diagnose() { findings_ = diagnosis_.Run({ registry_, frameRegions_ }); }

    // How long the scene's update took before the frame being presented; empty outside Play Mode.
    void NoteGameTick(std::optional<double> milliseconds) noexcept { gameTickMilliseconds_ = milliseconds; }
    [[nodiscard]] std::optional<double> GameTick() const noexcept { return gameTickMilliseconds_; }

    // Starts a recording of the counters now; the sampler stamps each frame with the time since.
    void StartRecording(std::vector<std::size_t> counters) {
        recorder_.Start(registry_, std::move(counters));
        recordingStartNanoseconds_ = kb::core::ProfilerCounters::NowNanoseconds();
    }
    [[nodiscard]] std::uint64_t RecordingStartNanoseconds() const noexcept { return recordingStartNanoseconds_; }

    // Forgets what was measured: the readings, the frames and the findings.
    void ClearMeasurements() {
        registry_.ClearHistory();
        frameRegions_.Clear();
        findings_.clear();
        panel_.selectedFrame.reset();
        panel_.selectedFinding = -1;
    }

    // Rebuilds the scene's model when the refresh interval has passed, or at once when asked to.
    void RefreshOverlay(const ProfilerSelection& selection,
        std::chrono::steady_clock::time_point now, bool force = false) {
        SyncThresholds(selection);
        const std::uint8_t rate = selection.overlay.refreshPerSecond;
        const bool due = rate == 0U || now - lastRefresh_ >= std::chrono::milliseconds{ 1000 / rate };
        if (!force && !due) return;
        overlay_ = BuildProfilerOverlayModel(registry_, selection);
        lastRefresh_ = now;
    }

private:
    kb::core::ProfilerRegistry registry_;
    kb::core::ProfilerCounters regions_{ true };
    kb::core::ProfilerFrameRegions frameRegions_;
    kb::core::ProfilerRecorder recorder_;
    kb::core::ProfilerDiagnosis diagnosis_;
    std::vector<kb::core::DiagnosisFinding> findings_;
    ProfilerPanelState panel_;
    ProfilerOverlayModel overlay_;
    std::vector<ProfilerThreshold> syncedThresholds_;
    std::chrono::steady_clock::time_point lastRefresh_{};
    std::uint64_t frame_ = 0U;
    std::uint64_t recordingStartNanoseconds_ = 0U;
    std::optional<double> gameTickMilliseconds_;
    bool synced_ = false;
    bool windowOpen_ = false;
};

} // namespace kb::editor

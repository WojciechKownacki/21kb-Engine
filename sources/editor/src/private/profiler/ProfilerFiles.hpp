#pragma once

#include "profiler/EditorProfilerState.hpp"

#include <string>

namespace kb::editor {

// Where the profiler writes: <project>/Saved/Profiler. Each function returns what to tell the user,
// the path it wrote or why it could not.
class ProfilerFiles final {
public:
    ProfilerFiles() = delete;

    // The recording, as the CSV of ProfilerRecorder.
    [[nodiscard]] static std::string SaveRecording(const EditorProfilerState& profiler);
    // The regions of one captured frame: frame, depth, name, total and own milliseconds, calls.
    [[nodiscard]] static std::string SaveSnapshot(const EditorProfilerState& profiler, std::uint64_t frame);
};

} // namespace kb::editor

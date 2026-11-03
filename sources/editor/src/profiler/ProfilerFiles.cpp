#include "profiler/ProfilerFiles.hpp"

#include "localization/EditorLocalization.hpp"
#include "project/EditorProjectPaths.hpp"

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>

namespace kb::editor {
namespace {

// Writes `text` to a new file `<kind>-<time>.csv` and says where it went.
[[nodiscard]] std::string Write(std::string_view kind, const std::string& text) {
    const std::filesystem::path folder = EditorProjectPaths::ProjectRoot() / "Saved" / "Profiler";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::filesystem::path path = folder / std::format("{}-{:%Y%m%d-%H%M%S}.csv", kind, now);
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    if (error || !output.is_open() || !(output << text).flush()) {
        return EditorLocalization::Format("profiler.panel.save_failed", "Could not write {path}", { { "path", path.generic_string() } });
    }
    return EditorLocalization::Format("profiler.panel.saved", "Saved {path}", { { "path", "Saved/Profiler/" + path.filename().generic_string() } });
}

} // namespace

std::string ProfilerFiles::SaveRecording(const EditorProfilerState& profiler) {
    return Write("recording", profiler.Recorder().ToCsv(profiler.Registry()));
}

std::string ProfilerFiles::SaveSnapshot(const EditorProfilerState& profiler, std::uint64_t frame) {
    const kb::core::ProfilerFrameRecord* record = profiler.FrameRegions().Find(frame);
    if (record == nullptr) return std::string{ EditorText("profiler.panel.no_snapshot", "That frame has no regions to save.") };
    std::string csv = "frame,depth,name,total_ms,self_ms,calls\n";
    for (const kb::core::ProfilerRegionRow& row : record->rows) {
        csv += std::format("{},{},{},{:.4f},{:.4f},{}\n", record->frame, row.depth, kb::core::ProfilerRecorder::CsvField(row.name),
            static_cast<double>(row.totalNanoseconds) / 1e6, static_cast<double>(row.selfNanoseconds) / 1e6, row.calls);
    }
    return Write("frame-snapshot", csv);
}

} // namespace kb::editor

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace kb::project {

// Whether the process that wrote a marker file still runs.
enum class AgentMarkerState : std::uint8_t {
    Absent,
    // The writing process runs (the same process counts: its pid and start time match).
    Alive,
    // The file names a process that is gone, or whose pid now belongs to another process.
    Dead,
    // The file cannot be read as a marker.
    Damaged,
};

struct AgentProcessMarker {
    AgentMarkerState state = AgentMarkerState::Absent;
    std::uint64_t pid = 0U;
    // Start time of that process in milliseconds, so a reused pid is told from the process that wrote the file.
    std::uint64_t started = 0U;
    // What the writer stored with it (a folder, a note).
    std::string detail;
    // Why a Damaged file cannot be used.
    std::string error;
};

// Writes the calling process's pid and start time and `detail` to `file` (its folder is made), all at once.
[[nodiscard]] bool WriteAgentProcessMarker(const std::filesystem::path& file, std::string_view detail, std::string& error);
[[nodiscard]] AgentProcessMarker ReadAgentProcessMarker(const std::filesystem::path& file);

} // namespace kb::project

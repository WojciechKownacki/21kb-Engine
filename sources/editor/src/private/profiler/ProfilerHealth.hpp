#pragma once

#include "engine/core/ProfilerRegistry.hpp"
#include "profiler/ProfilerSelection.hpp"

#include <string>
#include <vector>

namespace kb::editor {

// How the measured frames stand against their target, in one word.
enum class ProfilerHealthLevel : std::uint8_t {
    Waiting,   // nothing measured yet
    Smooth,    // every frame inside the budget
    Hitches,   // the usual frame fits, the slow ones do not
    BelowTarget,  // the usual frame is over the budget
    TooSlow,   // the usual frame is over the alarm
};

// One of the big numbers beside the verdict.
struct ProfilerHealthCard {
    std::string label;
    std::string value;
    std::string detail;
    kb::core::ProfilerSeverity severity = kb::core::ProfilerSeverity::Normal;
    bool available = false;
};

// What the profiler says before anyone reads a chart: a verdict in plain words, the reason
// behind it, and the handful of numbers most people look for.
struct ProfilerHealth {
    ProfilerHealthLevel level = ProfilerHealthLevel::Waiting;
    std::string headline;
    std::string advice;
    std::vector<ProfilerHealthCard> cards;
};

[[nodiscard]] ProfilerHealth BuildProfilerHealth(const kb::core::ProfilerRegistry& registry, const ProfilerSelection& selection);

} // namespace kb::editor

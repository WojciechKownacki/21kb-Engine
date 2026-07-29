#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace kb::library {

enum class DeterministicLibraryFeature : std::uint8_t {
    RandomStreams,
    Timers,
    ExecutionOrder,
    InputReplay,
    FixedSimulation,
};

enum class LibraryNonDeterminismReason : std::uint8_t {
    None,
    WallTime,
    Platform,
    AsyncIo,
    Rendering,
};

struct LibraryFunctionDeterminismInfo {
    bool deterministic = true;
    LibraryNonDeterminismReason reason = LibraryNonDeterminismReason::None;
};

struct DeterministicLibraryFeatureDesc {
    DeterministicLibraryFeature feature;
    std::string_view name;
    std::string_view contract;
};

// This is deliberately a subset, not a claim that the whole library is
// replay-safe. Callers must provide the same recorded input frames and fixed
// step; wall time, platform services, async I/O and rendering are excluded.
inline constexpr std::array<DeterministicLibraryFeatureDesc, 5U> kDeterministicLibraryFeatures{{
    { DeterministicLibraryFeature::RandomStreams, "RandomStreams", "Math.Random* advances explicit seed and counter state." },
    { DeterministicLibraryFeature::Timers, "Timers", "Timer state advances only from the supplied simulation delta." },
    { DeterministicLibraryFeature::ExecutionOrder, "ExecutionOrder", "Behaviours order by tick group, execution order, then entity id." },
    { DeterministicLibraryFeature::InputReplay, "InputReplay", "Recorded device state and per-frame delta reproduce input evaluation." },
    { DeterministicLibraryFeature::FixedSimulation, "FixedSimulation", "FixedTick and physics consume the same configured fixed step." },
}};

[[nodiscard]] constexpr bool IsDeterministicLibraryFeature(DeterministicLibraryFeature feature) noexcept {
    for (const DeterministicLibraryFeatureDesc& desc : kDeterministicLibraryFeatures) {
        if (desc.feature == feature) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] constexpr LibraryFunctionDeterminismInfo ClassifyLibraryFunctionDeterminism(std::string_view canonicalName) noexcept {
    if (canonicalName == "Time.Delta" || canonicalName == "Time.UnscaledDelta" || canonicalName == "Time.Elapsed" || canonicalName == "Time.FrameIndex") {
        return { .deterministic = false, .reason = LibraryNonDeterminismReason::WallTime };
    }
    if (canonicalName.starts_with("Input.") || canonicalName.starts_with("Audio.")) {
        return { .deterministic = false, .reason = LibraryNonDeterminismReason::Platform };
    }
    if (canonicalName.starts_with("Assets.")) {
        return { .deterministic = false, .reason = LibraryNonDeterminismReason::AsyncIo };
    }
    if (canonicalName.starts_with("Renderer.")) {
        return { .deterministic = false, .reason = LibraryNonDeterminismReason::Rendering };
    }
    return {};
}

} // namespace kb::library

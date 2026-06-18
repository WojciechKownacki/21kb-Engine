#pragma once

#include "engine/ecs/WorldConfigPresets.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace kb::ecs {

enum class WorldProfile : std::uint8_t {
    Mobile4K,
    Mobile8K,
    Mobile16K,
    Balanced32K,
    Desktop64K,
    Streaming128KPlus,
    BenchmarkAuto,
};

enum class WorldProfileParseError : std::uint8_t {
    None,
    Empty,
    UnsupportedValue,
};

struct WorldProfileParseResult {
    WorldProfile profile = WorldProfile::Balanced32K;
    WorldProfileParseError error = WorldProfileParseError::None;

    [[nodiscard]] constexpr bool HasValue() const noexcept {
        return error == WorldProfileParseError::None;
    }
};

[[nodiscard]] constexpr char NormalizeWorldProfileCharacter(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
}

[[nodiscard]] constexpr bool WorldProfileTokenEquals(std::string_view value, std::string_view expected) noexcept {
    if (value.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (NormalizeWorldProfileCharacter(value[index]) != NormalizeWorldProfileCharacter(expected[index])) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] constexpr WorldProfileParseResult ParseWorldProfileWithDiagnostics(std::string_view value) noexcept {
    if (value.empty()) {
        return WorldProfileParseResult{ .error = WorldProfileParseError::Empty };
    }
    if (WorldProfileTokenEquals(value, "mobile4k") ||
        WorldProfileTokenEquals(value, "mobile_4k") ||
        WorldProfileTokenEquals(value, "mobile-4k")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Mobile4K };
    }
    if (WorldProfileTokenEquals(value, "mobile8k") ||
        WorldProfileTokenEquals(value, "mobile_8k") ||
        WorldProfileTokenEquals(value, "mobile-8k")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Mobile8K };
    }
    if (WorldProfileTokenEquals(value, "mobile16k") ||
        WorldProfileTokenEquals(value, "mobile_16k") ||
        WorldProfileTokenEquals(value, "mobile-16k")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Mobile16K };
    }
    if (WorldProfileTokenEquals(value, "balanced32k") ||
        WorldProfileTokenEquals(value, "balanced_32k") ||
        WorldProfileTokenEquals(value, "balanced-32k")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Balanced32K };
    }
    if (WorldProfileTokenEquals(value, "desktop64k") ||
        WorldProfileTokenEquals(value, "desktop_64k") ||
        WorldProfileTokenEquals(value, "desktop-64k")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Desktop64K };
    }
    if (WorldProfileTokenEquals(value, "streaming128kplus") ||
        WorldProfileTokenEquals(value, "streaming_128k_plus") ||
        WorldProfileTokenEquals(value, "streaming-128k-plus")) {
        return WorldProfileParseResult{ .profile = WorldProfile::Streaming128KPlus };
    }
    if (WorldProfileTokenEquals(value, "benchmarkauto") ||
        WorldProfileTokenEquals(value, "benchmark_auto") ||
        WorldProfileTokenEquals(value, "benchmark-auto")) {
        return WorldProfileParseResult{ .profile = WorldProfile::BenchmarkAuto };
    }
    return WorldProfileParseResult{ .error = WorldProfileParseError::UnsupportedValue };
}

[[nodiscard]] constexpr std::string_view WorldProfileName(WorldProfile profile) noexcept {
    switch (profile) {
    case WorldProfile::Mobile4K:
        return "mobile4k";
    case WorldProfile::Mobile8K:
        return "mobile8k";
    case WorldProfile::Mobile16K:
        return "mobile16k";
    case WorldProfile::Balanced32K:
        return "balanced32k";
    case WorldProfile::Desktop64K:
        return "desktop64k";
    case WorldProfile::Streaming128KPlus:
        return "streaming128kplus";
    case WorldProfile::BenchmarkAuto:
        return "benchmarkauto";
    }
    return "balanced32k";
}

[[nodiscard]] constexpr WorldConfig WorldProfileConfig(WorldProfile profile) noexcept {
    switch (profile) {
    case WorldProfile::Mobile4K:
        return WorldConfigPresets::Mobile4K();
    case WorldProfile::Mobile8K:
        return WorldConfigPresets::Mobile8K();
    case WorldProfile::Mobile16K:
        return WorldConfigPresets::Mobile16K();
    case WorldProfile::Balanced32K:
        return WorldConfigPresets::Balanced32K();
    case WorldProfile::Desktop64K:
        return WorldConfigPresets::Desktop64K();
    case WorldProfile::Streaming128KPlus:
        return WorldConfigPresets::Streaming128KPlus();
    case WorldProfile::BenchmarkAuto:
        return WorldConfigPresets::BenchmarkAuto();
    }
    return WorldConfigPresets::Balanced32K();
}

} // namespace kb::ecs

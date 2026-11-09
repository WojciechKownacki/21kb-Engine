#pragma once

#include "engine/ecs/ComponentSerialization.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

enum class TimelinePropertyInterpolation : std::uint8_t { Step, Linear };

struct TimelinePropertyBinding {
    std::uint32_t version = 1U;
    std::string component;
    std::string field;
    kb::ecs::ComponentFieldType type = kb::ecs::ComponentFieldType::Float32;
    // Stable reflection signature; excludes native IDs, offsets and padding.
    std::uint64_t componentSchema = 0U;
};

struct TimelinePropertyKeyframe {
    float timeSeconds = 0.0F;
    kb::ecs::ComponentFieldValue value;
};

struct TimelinePropertyTrack {
    std::string binding;
    TimelinePropertyBinding property;
    TimelinePropertyInterpolation interpolation = TimelinePropertyInterpolation::Linear;
    std::vector<TimelinePropertyKeyframe> keyframes;
};

[[nodiscard]] std::uint64_t TimelineComponentSchema(
    const kb::ecs::ComponentReflection& reflection) noexcept;
[[nodiscard]] bool ValidateTimelinePropertyBinding(
    const TimelinePropertyBinding& binding, const kb::ecs::ComponentReflection& reflection,
    std::string& error);
[[nodiscard]] bool ValidateTimelinePropertyTrack(
    const TimelinePropertyTrack& track, float durationSeconds, std::string& error);
[[nodiscard]] bool SampleTimelineProperty(
    const TimelinePropertyTrack& track, float timeSeconds,
    kb::ecs::ComponentFieldValue& value, std::string& error);

} // namespace kb::scene

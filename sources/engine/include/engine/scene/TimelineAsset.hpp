#pragma once

#include "engine/scene/TransformComponent.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kb::scene {

using TimelineMarkerId = std::uint64_t;

struct TimelineBindingDefinition {
    std::string name;
    // "." binds the instance owner. Other values are slash-separated child
    // names relative to that owner and may be overridden at runtime.
    std::string defaultPath;
};

struct TimelineTransformKeyframe {
    float timeSeconds = 0.0F;
    LocalTransform transform{};
};

struct TimelineTransformTrack {
    std::string binding;
    std::vector<TimelineTransformKeyframe> keyframes;
};

struct TimelineMarker {
    float timeSeconds = 0.0F;
    TimelineMarkerId id = 0U;
};

// Sole authored definition of a timeline. Runtime instances retain this asset
// and derive only playhead state plus resolved SceneEntity bindings.
struct TimelineAsset {
    float durationSeconds = 1.0F;
    std::vector<TimelineBindingDefinition> bindings;
    std::vector<TimelineTransformTrack> transformTracks;
    std::vector<TimelineMarker> markers;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/TimelinePropertyTrack.hpp"
#include "engine/scene/TimelineAudioTrack.hpp"

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

struct TimelineEventKeyframe {
    float timeSeconds = 0.0F;
    TimelineMarkerId id = 0U;
    std::string name;
    std::string payload;
};

struct TimelineEventTrack {
    std::string binding;
    std::vector<TimelineEventKeyframe> keyframes;
};

struct TimelineSequenceBinding {
    std::string child;
    std::string parent;
};

struct TimelineSequenceTrack {
    std::string sequenceReference;
    float startSeconds = 0.0F;
    float endSeconds = 1.0F;
    float sourceOffsetSeconds = 0.0F;
    float rate = 1.0F;
    // Every child binding must map to a binding in the containing sequence.
    std::vector<TimelineSequenceBinding> bindings;
};

struct TimelineAnimationTrack {
    std::string binding;
    std::string clipReference;
    float startSeconds = 0.0F;
    float endSeconds = 1.0F;
    float sourceOffsetSeconds = 0.0F;
    float rate = 1.0F;
};

// Sole authored definition of a timeline. Runtime instances retain this asset
// and derive only playhead state plus resolved SceneEntity bindings.
struct TimelineAsset {
    float durationSeconds = 1.0F;
    std::vector<TimelineBindingDefinition> bindings;
    std::vector<TimelineTransformTrack> transformTracks;
    std::vector<TimelineMarker> markers;
    std::vector<TimelinePropertyTrack> propertyTracks;
    std::vector<TimelineAudioTrack> audioTracks;
    std::vector<TimelineEventTrack> eventTracks;
    std::vector<TimelineSequenceTrack> sequenceTracks;
    std::vector<TimelineAnimationTrack> animationTracks;
};

} // namespace kb::scene

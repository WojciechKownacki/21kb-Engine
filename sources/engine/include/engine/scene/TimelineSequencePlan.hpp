#pragma once

#include "engine/assets/AssetHandle.hpp"
#include "engine/scene/TimelineAsset.hpp"

#include <functional>
#include <map>
#include <optional>

namespace kb::scene {

struct TimelineSequenceSegment {
    kb::assets::AssetHandle<TimelineAsset> asset;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    // Source time = sourceOffsetSeconds + (root time - startSeconds) * rate.
    double sourceOffsetSeconds = 0.0;
    double rate = 1.0;
    std::map<std::string, std::string, std::less<>> rootBindings;
};

using TimelineSequenceResolver = std::function<kb::assets::AssetHandle<TimelineAsset>(const std::string&)>;

// Produces retained source segments, without baking away interpolation or events.
// Rejects cycles, invalid remaps and excessive nesting before playback starts.
[[nodiscard]] std::optional<std::vector<TimelineSequenceSegment>> BuildTimelineSequencePlan(
    const kb::assets::AssetHandle<TimelineAsset>& root, const TimelineSequenceResolver& resolve, std::string& error);

} // namespace kb::scene

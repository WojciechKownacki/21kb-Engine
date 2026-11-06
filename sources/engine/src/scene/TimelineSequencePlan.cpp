#include "engine/scene/TimelineSequencePlan.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kb::scene {
namespace {
constexpr std::size_t kMaxDepth = 16;
constexpr std::size_t kMaxSegments = 256;

bool Expand(const TimelineSequenceSegment& parent, const TimelineSequenceResolver& resolve,
    std::vector<std::uint64_t>& ancestors, std::vector<TimelineSequenceSegment>& output,
    std::size_t& visited, std::string& error) {
    if (++visited > kMaxSegments || ancestors.size() >= kMaxDepth) {
        error = "Timeline sequence expansion exceeded its depth or segment limit";
        return false;
    }
    const auto id = parent.asset.Id().value;
    if (std::find(ancestors.begin(), ancestors.end(), id) != ancestors.end()) {
        error = "Timeline sequence dependency cycle";
        return false;
    }
    ancestors.push_back(id);
    if (parent.endSeconds > parent.startSeconds) output.push_back(parent);
    for (const auto& track : parent.asset->sequenceTracks) {
        auto childAsset = resolve(track.sequenceReference);
        if (!childAsset.IsLoaded()) {
            error = "Timeline sequence could not be resolved: " + track.sequenceReference;
            return false;
        }
        const double sourceEnd = static_cast<double>(track.sourceOffsetSeconds) +
            (static_cast<double>(track.endSeconds) - track.startSeconds) * track.rate;
        if (!std::isfinite(sourceEnd) || track.rate <= 0.0F || track.sourceOffsetSeconds < 0.0F ||
            track.startSeconds < 0.0F || track.endSeconds <= track.startSeconds ||
            track.endSeconds > parent.asset->durationSeconds || sourceEnd > childAsset->durationSeconds) {
            error = "Timeline sequence interval exceeds its source or parent duration";
            return false;
        }
        TimelineSequenceSegment child;
        child.asset = std::move(childAsset);
        const double unclippedStart = parent.startSeconds + (track.startSeconds - parent.sourceOffsetSeconds) / parent.rate;
        const double unclippedEnd = parent.startSeconds + (track.endSeconds - parent.sourceOffsetSeconds) / parent.rate;
        child.startSeconds = std::max(parent.startSeconds, unclippedStart);
        child.endSeconds = std::min(parent.endSeconds, unclippedEnd);
        child.rate = parent.rate * track.rate;
        child.sourceOffsetSeconds = track.sourceOffsetSeconds + (child.startSeconds - unclippedStart) * child.rate;
        if (!std::isfinite(child.rate) || child.rate <= 0.0 || !std::isfinite(child.sourceOffsetSeconds)) {
            error = "Timeline sequence time mapping is not finite";
            return false;
        }
        for (const auto& binding : track.bindings) {
            const auto parentBinding = parent.rootBindings.find(binding.parent);
            const auto childBinding = std::find_if(child.asset->bindings.begin(), child.asset->bindings.end(),
                [&](const auto& definition) { return definition.name == binding.child; });
            if (parentBinding == parent.rootBindings.end() || childBinding == child.asset->bindings.end() ||
                !child.rootBindings.emplace(binding.child, parentBinding->second).second) {
                error = "Timeline sequence binding remap is invalid";
                return false;
            }
        }
        if (child.rootBindings.size() != child.asset->bindings.size()) {
            error = "Timeline sequence requires a remap for every child binding";
            return false;
        }
        if (!Expand(child, resolve, ancestors, output, visited, error)) return false;
    }
    ancestors.pop_back();
    return true;
}
} // namespace

std::optional<std::vector<TimelineSequenceSegment>> BuildTimelineSequencePlan(
    const kb::assets::AssetHandle<TimelineAsset>& root, const TimelineSequenceResolver& resolve, std::string& error) {
    error.clear();
    if (!root.IsLoaded() || !resolve || !std::isfinite(root->durationSeconds) || root->durationSeconds <= 0.0F) {
        error = "Timeline sequence root or resolver is invalid";
        return std::nullopt;
    }
    TimelineSequenceSegment segment;
    segment.asset = root;
    segment.endSeconds = root->durationSeconds;
    for (const auto& binding : root->bindings) segment.rootBindings.emplace(binding.name, binding.name);
    std::vector<std::uint64_t> ancestors;
    std::vector<TimelineSequenceSegment> output;
    std::size_t visited = 0;
    if (!Expand(segment, resolve, ancestors, output, visited, error)) return std::nullopt;
    return output;
}
} // namespace kb::scene

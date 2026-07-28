#include "scene/SceneTimelineService.hpp"

#include "engine/assets/AssetManager.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace kb::scene {
namespace {

TimelineRuntimeRecord* Find(SceneState& state, std::uint64_t instance) {
    const auto found = state.timelines.find(instance);
    return found == state.timelines.end() ? nullptr : &found->second;
}

const TimelineRuntimeRecord* Find(
    const SceneState& state, std::uint64_t instance) {
    const auto found = state.timelines.find(instance);
    return found == state.timelines.end() ? nullptr : &found->second;
}

SceneEntity ResolveTarget(
    Scene& scene, SceneEntity owner, std::string_view path) {
    if (path.empty() || path == ".") return owner;
    SceneEntity current = owner;
    std::size_t offset = 0U;
    while (offset < path.size()) {
        const std::size_t separator = path.find('/', offset);
        const std::string_view segment = path.substr(
            offset, separator == std::string_view::npos
                ? path.size() - offset
                : separator - offset);
        if (segment.empty()) return {};
        SceneEntity next{};
        for (const SceneEntity child :
             scene.Hierarchy().ChildEntities(current)) {
            if (scene.Entities().Name(child) == segment) {
                if (next.IsValid()) return {};
                next = child;
            }
        }
        if (!next.IsValid()) return {};
        current = next;
        if (separator == std::string_view::npos) break;
        offset = separator + 1U;
    }
    return current;
}

[[nodiscard]] LocalTransform Sample(
    const TimelineTransformTrack& track, float timeSeconds) {
    if (timeSeconds <= track.keyframes.front().timeSeconds) {
        return track.keyframes.front().transform;
    }
    if (timeSeconds >= track.keyframes.back().timeSeconds) {
        return track.keyframes.back().transform;
    }
    const auto upper = std::upper_bound(
        track.keyframes.begin(), track.keyframes.end(), timeSeconds,
        [](float value, const TimelineTransformKeyframe& keyframe) {
            return value < keyframe.timeSeconds;
        });
    const TimelineTransformKeyframe& to = *upper;
    const TimelineTransformKeyframe& from = *(upper - 1);
    const float alpha = (timeSeconds - from.timeSeconds) /
        (to.timeSeconds - from.timeSeconds);
    const auto lerp = [alpha](float lhs, float rhs) {
        return lhs + (rhs - lhs) * alpha;
    };
    return LocalTransform{
        .position = {
            lerp(from.transform.position.x, to.transform.position.x),
            lerp(from.transform.position.y, to.transform.position.y),
            lerp(from.transform.position.z, to.transform.position.z),
        },
        .rotation = kb::math::Slerp(
            from.transform.rotation, to.transform.rotation, alpha),
        .scale = {
            lerp(from.transform.scale.x, to.transform.scale.x),
            lerp(from.transform.scale.y, to.transform.scale.y),
            lerp(from.transform.scale.z, to.transform.scale.z),
        },
    };
}

[[nodiscard]] bool ResolveBindings(
    Scene& scene, TimelineRuntimeRecord& record,
    const std::map<std::string, TimelineRuntimeBinding, std::less<>>*
        previous = nullptr) {
    record.bindings.clear();
    for (const TimelineBindingDefinition& definition :
         record.asset->bindings) {
        TimelineRuntimeBinding binding{};
        const auto old = previous == nullptr
            ? std::map<std::string, TimelineRuntimeBinding, std::less<>>::const_iterator{}
            : previous->find(definition.name);
        if (previous != nullptr && old != previous->end() &&
            old->second.explicitlyBound &&
            scene.Entities().IsAlive(old->second.target)) {
            binding = old->second;
        } else {
            binding.target =
                ResolveTarget(scene, record.owner, definition.defaultPath);
        }
        if (!binding.target.IsValid() ||
            scene.Transforms().TryGet(binding.target) == nullptr) {
            return false;
        }
        record.bindings.emplace(definition.name, binding);
    }
    record.observedHierarchyTopologyVersion =
        SceneAccess::State(scene).hierarchyTopologyVersion;
    return true;
}

[[nodiscard]] bool ApplyPose(
    Scene& scene, const TimelineRuntimeRecord& record) {
    for (const TimelineTransformTrack& track :
         record.asset->transformTracks) {
        const auto binding = record.bindings.find(track.binding);
        if (binding == record.bindings.end() ||
            !scene.Entities().IsAlive(binding->second.target)) {
            return false;
        }
        TransformComponent* transform =
            scene.Transforms().TryGet(binding->second.target);
        if (transform == nullptr) return false;
        const LocalTransform sampled = Sample(track, record.timeSeconds);
        transform->localPosition = sampled.position;
        transform->localRotation = sampled.rotation;
        transform->localScale = sampled.scale;
        scene.Transforms().MarkModified(binding->second.target);
    }
    return true;
}

void QueueMarkers(
    SceneState& state, const TimelineRuntimeRecord& record,
    float fromExclusive, float toInclusive) {
    const auto first = std::upper_bound(
        record.asset->markers.begin(), record.asset->markers.end(),
        fromExclusive,
        [](float value, const TimelineMarker& marker) {
            return value < marker.timeSeconds;
        });
    const auto last = std::upper_bound(
        first, record.asset->markers.end(), toInclusive,
        [](float value, const TimelineMarker& marker) {
            return value < marker.timeSeconds;
        });
    const std::size_t count =
        static_cast<std::size_t>(last - first);
    if (count >
        SceneTimelines::kMaxPendingMarkers -
            state.pendingTimelineMarkerEvents.size()) {
        throw std::length_error(
            "Timeline marker queue exceeded its fixed capacity");
    }
    for (auto marker = first; marker != last; ++marker) {
        state.pendingTimelineMarkerEvents.push_back(TimelineMarkerEvent{
            .target = record.owner,
            .instanceId = record.id,
            .assetId = record.asset.Id().value,
            .markerId = marker->id,
            .timeSeconds = marker->timeSeconds,
        });
    }
}

[[nodiscard]] bool Refresh(
    Scene& scene, TimelineRuntimeRecord& record) {
    SceneState& state = SceneAccess::State(scene);
    const std::uint64_t generation =
        scene.Assets().Manager().LoadGeneration(record.asset.Id());
    const bool assetChanged = generation != record.assetLoadGeneration;
    const bool hierarchyChanged =
        record.observedHierarchyTopologyVersion !=
        state.hierarchyTopologyVersion;
    if (!assetChanged && !hierarchyChanged) return true;

    auto previousBindings = record.bindings;
    if (assetChanged) {
        auto asset =
            scene.Assets().Manager().Load<TimelineAsset>(record.asset.Id());
        if (!asset.IsLoaded()) return false;
        record.asset = std::move(asset);
        record.assetLoadGeneration = generation;
        record.timeSeconds = std::clamp(
            record.timeSeconds, 0.0F, record.asset->durationSeconds);
        if (record.timeSeconds >= record.asset->durationSeconds) {
            record.playing = false;
        }
    }
    return ResolveBindings(scene, record, &previousBindings) &&
        ApplyPose(scene, record);
}

} // namespace

std::uint64_t SceneTimelineService::Create(
    Scene& scene, std::uint64_t assetId, SceneEntity owner) {
    SceneState& state = SceneAccess::State(scene);
    if (!scene.Entities().IsAlive(owner) ||
        state.timelines.size() >= SceneTimelines::kMaxInstances ||
        state.nextTimelineInstanceId == 0U) {
        return 0U;
    }
    auto asset = scene.Assets().Manager().Load<TimelineAsset>(
        kb::assets::AssetId{ assetId });
    if (!asset.IsLoaded()) return 0U;
    TimelineRuntimeRecord record{
        .id = state.nextTimelineInstanceId,
        .owner = owner,
        .asset = std::move(asset),
    };
    record.assetLoadGeneration =
        scene.Assets().Manager().LoadGeneration(record.asset.Id());
    if (!ResolveBindings(scene, record) || !ApplyPose(scene, record)) {
        return 0U;
    }
    const std::uint64_t id = record.id;
    ++state.nextTimelineInstanceId;
    state.timelines.emplace(id, std::move(record));
    return id;
}

bool SceneTimelineService::Release(
    Scene& scene, std::uint64_t instance) noexcept {
    return SceneAccess::State(scene).timelines.erase(instance) > 0U;
}

bool SceneTimelineService::Exists(
    const Scene& scene, std::uint64_t instance) noexcept {
    return Find(SceneAccess::State(scene), instance) != nullptr;
}

bool SceneTimelineService::Play(
    Scene& scene, std::uint64_t instance) noexcept {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr ||
        record->timeSeconds >= record->asset->durationSeconds) {
        return false;
    }
    record->playing = true;
    return true;
}

bool SceneTimelineService::Pause(
    Scene& scene, std::uint64_t instance) noexcept {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr) return false;
    record->playing = false;
    return true;
}

bool SceneTimelineService::IsPlaying(
    const Scene& scene, std::uint64_t instance) noexcept {
    const TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    return record != nullptr && record->playing;
}

bool SceneTimelineService::Seek(
    Scene& scene, std::uint64_t instance, float timeSeconds) {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr || !std::isfinite(timeSeconds) ||
        timeSeconds < 0.0F ||
        timeSeconds > record->asset->durationSeconds) {
        return false;
    }
    record->timeSeconds = timeSeconds;
    if (timeSeconds >= record->asset->durationSeconds) {
        record->playing = false;
    }
    return ApplyPose(scene, *record);
}

bool SceneTimelineService::Skip(
    Scene& scene, std::uint64_t instance, float targetTimeSeconds,
    TimelineSkipMarkerPolicy markerPolicy) {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr || !std::isfinite(targetTimeSeconds) ||
        targetTimeSeconds < record->timeSeconds ||
        targetTimeSeconds > record->asset->durationSeconds) {
        return false;
    }
    if (markerPolicy == TimelineSkipMarkerPolicy::EmitCrossed) {
        QueueMarkers(
            SceneAccess::State(scene), *record, record->timeSeconds,
            targetTimeSeconds);
    } else if (markerPolicy != TimelineSkipMarkerPolicy::Suppress) {
        return false;
    }
    record->timeSeconds = targetTimeSeconds;
    if (targetTimeSeconds >= record->asset->durationSeconds) {
        record->playing = false;
    }
    return ApplyPose(scene, *record);
}

bool SceneTimelineService::Bind(
    Scene& scene, std::uint64_t instance, const std::string& binding,
    SceneEntity target) {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr || !scene.Entities().IsAlive(target) ||
        scene.Transforms().TryGet(target) == nullptr) {
        return false;
    }
    const auto found = record->bindings.find(binding);
    if (found == record->bindings.end()) return false;
    found->second =
        TimelineRuntimeBinding{ .target = target, .explicitlyBound = true };
    return ApplyPose(scene, *record);
}

float SceneTimelineService::Time(
    const Scene& scene, std::uint64_t instance) noexcept {
    const TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    return record == nullptr ? 0.0F : record->timeSeconds;
}

std::uint64_t SceneTimelineService::Asset(
    const Scene& scene, std::uint64_t instance) noexcept {
    const TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    return record == nullptr ? 0U : record->asset.Id().value;
}

std::vector<TimelineMarkerEvent>
SceneTimelineService::DrainMarkerEvents(Scene& scene) {
    std::vector<TimelineMarkerEvent> drained;
    drained.swap(SceneAccess::State(scene).pendingTimelineMarkerEvents);
    return drained;
}

void SceneTimelineService::Advance(Scene& scene, float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        throw std::invalid_argument(
            "TimelineSceneSystem requires a finite non-negative delta");
    }
    SceneState& state = SceneAccess::State(scene);
    for (auto iterator = state.timelines.begin();
         iterator != state.timelines.end();) {
        TimelineRuntimeRecord& record = iterator->second;
        if (!scene.Entities().IsAlive(record.owner)) {
            iterator = state.timelines.erase(iterator);
            continue;
        }
        if (!Refresh(scene, record)) {
            iterator = state.timelines.erase(iterator);
            throw std::runtime_error(
                "Timeline asset reload or binding resolution failed");
        }
        if (record.playing && scene.Runtime().IsPlaying() &&
            deltaSeconds > 0.0F) {
            const float previous = record.timeSeconds;
            record.timeSeconds = std::min(
                record.asset->durationSeconds,
                record.timeSeconds + deltaSeconds);
            QueueMarkers(state, record, previous, record.timeSeconds);
            if (!ApplyPose(scene, record)) {
                iterator = state.timelines.erase(iterator);
                throw std::runtime_error(
                    "Timeline lost an active Transform binding");
            }
            if (record.timeSeconds >= record.asset->durationSeconds) {
                record.playing = false;
            }
        }
        ++iterator;
    }
}

} // namespace kb::scene

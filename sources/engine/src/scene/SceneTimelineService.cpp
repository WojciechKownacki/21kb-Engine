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
#include "scene/SceneAnimatorService.hpp"
#include "scene/SceneState.hpp"
#include "ecs/world/WorldInternalAccess.hpp"
#include "ecs/serialization/ComponentFieldValueWriter.hpp"

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

void BuildSegments(Scene& scene, TimelineRuntimeRecord& record) {
    auto& manager = scene.Assets().Manager();
    std::string error;
    auto plan = BuildTimelineSequencePlan(record.asset, [&](const std::string& reference) {
        kb::assets::AssetId id;
        const auto* metadata = kb::assets::TryParseAssetId(reference, id) && id.IsValid()
            ? manager.Registry().Find(id) : manager.Registry().FindByPath(std::filesystem::path{reference});
        return metadata == nullptr ? kb::assets::AssetHandle<TimelineAsset>{} : manager.Load<TimelineAsset>(metadata->id);
    }, error);
    if (!plan) throw std::runtime_error(error);
    std::vector<TimelineRuntimeSegment> segments;
    for (auto& source : *plan) {
        std::vector<TimelineRuntimeAnimation> animations;
        for (const auto& track : source.asset->animationTracks) {
            kb::assets::AssetId clipId;
            const auto* metadata = kb::assets::TryParseAssetId(track.clipReference, clipId) && clipId.IsValid()
                ? manager.Registry().Find(clipId) : manager.Registry().FindByPath(std::filesystem::path{track.clipReference});
            auto clip = metadata == nullptr ? kb::assets::AssetHandle<AnimationClip>{} : manager.Load<AnimationClip>(metadata->id);
            const double end = track.sourceOffsetSeconds +
                (static_cast<double>(track.endSeconds) - track.startSeconds) * track.rate;
            if (!clip.IsLoaded() || !std::isfinite(end) || end > clip->durationSeconds)
                throw std::runtime_error("Timeline animation interval exceeds its source clip: " + track.clipReference);
            const double start = source.startSeconds + (track.startSeconds - source.sourceOffsetSeconds) / source.rate;
            const double finish = source.startSeconds + (track.endSeconds - source.sourceOffsetSeconds) / source.rate;
            TimelineRuntimeAnimation animation;
            animation.asset = std::move(clip);
            animation.assetLoadGeneration = manager.LoadGeneration(animation.asset.Id());
            animation.binding = track.binding;
            animation.startSeconds = std::max(source.startSeconds, start);
            animation.endSeconds = std::min(source.endSeconds, finish);
            animation.rate = source.rate * track.rate;
            animation.sourceOffsetSeconds = track.sourceOffsetSeconds + (animation.startSeconds - start) * animation.rate;
            if (!std::isfinite(animation.rate) || animation.rate <= 0.0 || !std::isfinite(animation.sourceOffsetSeconds))
                throw std::runtime_error("Timeline animation time mapping is not finite");
            animations.push_back(std::move(animation));
        }
        const auto generation = manager.LoadGeneration(source.asset.Id());
        segments.push_back({.source = std::move(source), .assetLoadGeneration = generation, .animations = std::move(animations)});
    }
    record.segments = std::move(segments);
    record.signalCount = 0;
    for (const auto& segment : record.segments) {
        const auto count = [&](const auto& keys, const auto& source) {
            for (const auto& key : keys) {
                const double time = source.startSeconds +
                    (key.timeSeconds - source.sourceOffsetSeconds) / source.rate;
                if (time > 0.0 && time >= source.startSeconds && time <= source.endSeconds)
                    ++record.signalCount;
            }
        };
        count(segment.source.asset->markers, segment.source);
        for (const auto& track : segment.source.asset->eventTracks) count(track.keyframes, segment.source);
        for (const auto& animation : segment.animations) count(animation.asset->events, animation);
    }
}

void BindSegments(TimelineRuntimeRecord& record) {
    for (auto& segment : record.segments) {
        segment.bindings.clear();
        for (const auto& [child, parent] : segment.source.rootBindings)
            segment.bindings.emplace(child, record.bindings.at(parent));
    }
}

float SourceTime(const TimelineRuntimeSegment& segment, float rootTime) {
    return static_cast<float>(std::clamp(segment.source.sourceOffsetSeconds +
        (static_cast<double>(rootTime) - segment.source.startSeconds) * segment.source.rate,
        0.0, static_cast<double>(segment.source.asset->durationSeconds)));
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
    BindSegments(record);
    record.observedHierarchyTopologyVersion =
        SceneAccess::State(scene).hierarchyTopologyVersion;
    return true;
}

void StopAudio(Scene& scene, TimelineRuntimeRecord& record) noexcept {
    for (auto& segment : record.segments) {
        for (auto& voice : segment.audioVoices) TimelineAudioPlayback::Stop(scene, voice);
        segment.audioVoices.clear();
    }
    record.audioDiscontinuity = true;
}

void SynchronizeAudio(Scene& scene, TimelineRuntimeRecord& record) {
    const auto& registry = scene.Assets().Manager().Registry();
    for (auto& segment : record.segments) {
        const auto& asset = *segment.source.asset;
        if (segment.audioVoices.size() != asset.audioTracks.size()) segment.audioVoices.resize(asset.audioTracks.size());
        const bool active = record.timeSeconds >= segment.source.startSeconds && record.timeSeconds < segment.source.endSeconds;
        for (std::size_t index = 0; index < asset.audioTracks.size(); ++index) {
            auto& voice = segment.audioVoices[index];
            if (!active) { TimelineAudioPlayback::Stop(scene, voice); continue; }
            const auto& track = asset.audioTracks[index];
            const auto target = segment.bindings.find(track.binding);
            kb::assets::AssetId id;
            const auto* metadata = kb::assets::TryParseAssetId(track.clipReference, id) && id.IsValid()
                ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.clipReference});
            if (target == segment.bindings.end() || !scene.Entities().IsAlive(target->second.target) ||
                metadata == nullptr || !((metadata->type == "AudioClip" && metadata->importCategory.empty()) ||
                    (metadata->type == "ImportedAsset" && metadata->importCategory == "Audio")))
                throw std::runtime_error("Timeline audio binding or clip is unavailable: " + track.clipReference);
            std::string error;
            if (!TimelineAudioPlayback::Synchronize(scene, track, metadata->id.value, target->second.target,
                SourceTime(segment, record.timeSeconds), static_cast<float>(record.playbackRate * segment.source.rate),
                record.playing && scene.Runtime().IsPlaying(), record.audioDiscontinuity, voice, error))
                throw std::runtime_error(error);
        }
    }
    record.audioDiscontinuity = false;
}

[[nodiscard]] bool ApplySegmentPose(
    Scene& scene, const TimelineRuntimeSegment& segment, float timeSeconds) {
    auto& world = SceneAccess::State(scene).world;
    // Resolve against current reflection and component lifetime on every sample.
    // A saved binding never stores an address or a process-local component ID.
    for (const auto& track : segment.source.asset->propertyTracks) {
        const auto target = segment.bindings.find(track.binding);
        const auto* reflection = world.Reflection(track.property.component);
        std::string error;
        kb::ecs::ComponentFieldValue value;
        if (target == segment.bindings.end() || !scene.Entities().IsAlive(target->second.target))
            throw std::runtime_error("Timeline property target is unavailable: " + track.binding);
        if (reflection == nullptr)
            throw std::runtime_error("Timeline reflected component is unavailable: " + track.property.component);
        if (!ValidateTimelinePropertyBinding(track.property, *reflection, error) ||
            !SampleTimelineProperty(track, timeSeconds, value, error))
            throw std::runtime_error(error + ": " + track.property.component + "." + track.property.field);
        if (kb::ecs::WorldInternalAccess::TryGetComponent(world, target->second.target, reflection->Id()) == nullptr)
            throw std::runtime_error("Timeline component was removed: " + track.property.component);
    }
    for (const TimelineTransformTrack& track :
         segment.source.asset->transformTracks) {
        const auto binding = segment.bindings.find(track.binding);
        if (binding == segment.bindings.end() ||
            !scene.Entities().IsAlive(binding->second.target)) {
            return false;
        }
        TransformComponent* transform =
            scene.Transforms().TryGet(binding->second.target);
        if (transform == nullptr) return false;
        const LocalTransform sampled = Sample(track, timeSeconds);
        transform->localPosition = sampled.position;
        transform->localRotation = sampled.rotation;
        transform->localScale = sampled.scale;
        scene.Transforms().MarkModified(binding->second.target);
    }
    for (const auto& track : segment.source.asset->propertyTracks) {
        const auto target = segment.bindings.find(track.binding);
        const auto* reflection = world.Reflection(track.property.component);
        const auto* field = reflection == nullptr ? nullptr : reflection->FindField(track.property.field);
        if (field == nullptr || target == segment.bindings.end() || !scene.Entities().IsAlive(target->second.target))
            throw std::runtime_error("Timeline property binding changed during pose application");
        kb::ecs::ComponentFieldValue value;
        std::string error;
        if (!ValidateTimelinePropertyBinding(track.property, *reflection, error) ||
            !SampleTimelineProperty(track, timeSeconds, value, error)) throw std::runtime_error(error);
        void* component = kb::ecs::WorldInternalAccess::TryGetMutableComponent(world, target->second.target, reflection->Id());
        if (component == nullptr || !kb::ecs::ComponentFieldValueWriter::Write(component, reflection->Size(), *field, value))
            throw std::runtime_error("Timeline property could not be written: " + track.property.field);
        kb::ecs::WorldInternalAccess::MarkComponentModified(world, target->second.target, reflection->Id());
    }
    return true;
}

[[nodiscard]] bool ApplyPose(Scene& scene, const TimelineRuntimeRecord& record) {
    std::vector<SceneEntity> animated;
    try {
        for (const auto& segment : record.segments) {
            if (record.timeSeconds < segment.source.startSeconds || record.timeSeconds > segment.source.endSeconds) continue;
            const float time = SourceTime(segment, record.timeSeconds);
            if (!ApplySegmentPose(scene, segment, time)) throw std::runtime_error("Timeline pose binding is unavailable");
            for (const auto& track : segment.source.asset->animationTracks) {
                if (time < track.startSeconds || time > track.endSeconds ||
                    (time == track.endSeconds && record.timeSeconds < record.asset->durationSeconds)) continue;
                const auto target = segment.bindings.find(track.binding);
                if (target == segment.bindings.end()) throw std::runtime_error("Timeline animation binding is missing");
                if (std::find(animated.begin(), animated.end(), target->second.target) != animated.end())
                    throw std::runtime_error("Overlapping Timeline animation tracks cannot share a target");
                const auto& registry = scene.Assets().Manager().Registry();
                kb::assets::AssetId id;
                const auto* metadata = kb::assets::TryParseAssetId(track.clipReference, id) && id.IsValid()
                    ? registry.Find(id) : registry.FindByPath(std::filesystem::path{track.clipReference});
                const float sourceTime = track.sourceOffsetSeconds + (time - track.startSeconds) * track.rate;
                if (metadata == nullptr || metadata->type != "AnimationClip" ||
                    !SceneAnimatorService::SampleTimelineClip(scene, target->second.target, metadata->id.value, sourceTime, record.id))
                    throw std::runtime_error("Timeline animation clip could not be sampled: " + track.clipReference);
                animated.push_back(target->second.target);
            }
        }
        SceneAnimatorService::ReleaseTimelineClips(scene, record.id, animated);
        return true;
    } catch (...) {
        SceneAnimatorService::ReleaseTimelineClips(scene, record.id);
        throw;
    }
}

void QueueMarkers(
    SceneState& state, const TimelineRuntimeRecord& record,
    float fromExclusive, float toInclusive, bool reverseBoundary = false) {
    const bool reverse = toInclusive < fromExclusive;
    std::vector<TimelineMarkerEvent> crossed;
    const auto append = [&](const auto& keys, SceneEntity target, const auto& source, bool animationEvent) {
        const double from = source.sourceOffsetSeconds + (fromExclusive - source.startSeconds) * source.rate;
        const double to = source.sourceOffsetSeconds + (toInclusive - source.startSeconds) * source.rate;
        const auto lower = [](const auto& key, double time) { return key.timeSeconds < time; };
        const auto upper = [](double time, const auto& key) { return time < key.timeSeconds; };
        const auto first = reverse ? std::lower_bound(keys.begin(), keys.end(), to, lower)
                                   : std::upper_bound(keys.begin(), keys.end(), from, upper);
        const auto last = reverse ? std::lower_bound(first, keys.end(), from, lower)
                                  : std::upper_bound(first, keys.end(), to, upper);
        const auto available = SceneTimelines::kMaxPendingMarkers - state.pendingTimelineMarkerEvents.size();
        for (auto key = first; key != last; ++key) {
            const double rootTime = source.startSeconds + (key->timeSeconds - source.sourceOffsetSeconds) / source.rate;
            if (rootTime < source.startSeconds || rootTime > source.endSeconds || rootTime <= 0.0) continue;
            if (crossed.size() >= available) throw std::length_error("Timeline marker queue exceeded its fixed capacity");
            TimelineMarkerEvent event{
                .target = target, .instanceId = record.id, .assetId = source.asset.Id().value,
                .markerId = key->id, .timeSeconds = static_cast<float>(rootTime),
                .animationEvent = animationEvent, .normalizedTime = key->timeSeconds / source.asset->durationSeconds};
            if constexpr (requires { key->name; key->payload; }) {
                event.eventName = key->name;
                event.payload = key->payload;
            }
            crossed.push_back(std::move(event));
        }
    };
    for (const auto& segment : record.segments) {
        append(segment.source.asset->markers, record.owner, segment.source, false);
        for (const auto& track : segment.source.asset->eventTracks) {
            const auto target = segment.bindings.find(track.binding);
            if (target == segment.bindings.end()) throw std::runtime_error("Timeline event binding is missing: " + track.binding);
            append(track.keyframes, target->second.target, segment.source, false);
        }
        for (const auto& animation : segment.animations) {
            const auto target = segment.bindings.find(animation.binding);
            if (target == segment.bindings.end()) throw std::runtime_error("Timeline animation event binding is missing");
            append(animation.asset->events, target->second.target, animation, true);
        }
    }
    std::stable_sort(crossed.begin(), crossed.end(), [](const auto& left, const auto& right) {
        return left.timeSeconds < right.timeSeconds;
    });
    if (reverse || reverseBoundary) std::reverse(crossed.begin(), crossed.end());
    state.pendingTimelineMarkerEvents.insert(state.pendingTimelineMarkerEvents.end(), crossed.begin(), crossed.end());
}

// Queue a complete traversal before committing the cursor. Capacity failure
// leaves both the cursor and the existing event queue unchanged.
void AdvanceCursor(SceneState& state, TimelineRuntimeRecord& record, float deltaSeconds) {
    const double duration = record.asset->durationSeconds;
    const bool forward = record.playbackRate > 0.0F;
    double distance = static_cast<double>(deltaSeconds) * std::abs(static_cast<double>(record.playbackRate));
    double cursor = record.timeSeconds;
    const bool wraps = record.looping && distance > (forward ? duration - cursor : cursor);
    if (!record.looping) distance = std::min(distance, forward ? duration - cursor : cursor);
    const auto oldSize = state.pendingTimelineMarkerEvents.size();
    const std::size_t signalCount = record.signalCount;
    if (record.looping && signalCount != 0) {
        const double completeCycles = std::max(0.0, std::floor(distance / duration) - 1.0);
        if (completeCycles > static_cast<double>(SceneTimelines::kMaxPendingMarkers - oldSize) / signalCount)
            throw std::length_error("Timeline marker queue exceeded its fixed capacity");
    }
    try {
        if (record.looping && signalCount == 0) {
            cursor = std::fmod(cursor + (forward ? distance : -distance), duration);
            if (cursor < 0.0) cursor += duration;
            if (forward && cursor == 0.0 && distance > 0.0) cursor = duration;
        } else while (distance > 0.0) {
            if (forward && cursor == duration) cursor = 0.0;
            else if (!forward && cursor == 0.0) {
                cursor = duration;
                // The end key belongs to the newly entered reverse cycle.
                QueueMarkers(state, record, std::nextafter(static_cast<float>(duration), 0.0F), static_cast<float>(duration), true);
            }
            const double step = std::min(distance, forward ? duration - cursor : cursor);
            const double next = cursor + (forward ? step : -step);
            QueueMarkers(state, record, static_cast<float>(cursor), static_cast<float>(next));
            cursor = next;
            distance -= step;
        }
    } catch (...) {
        state.pendingTimelineMarkerEvents.resize(oldSize);
        throw;
    }
    record.timeSeconds = static_cast<float>(cursor);
    record.audioDiscontinuity = record.audioDiscontinuity || wraps;
    if (!record.looping && (forward ? cursor >= duration : cursor <= 0.0)) record.playing = false;
}

[[nodiscard]] bool Refresh(
    Scene& scene, TimelineRuntimeRecord& record) {
    SceneState& state = SceneAccess::State(scene);
    const std::uint64_t generation =
        scene.Assets().Manager().LoadGeneration(record.asset.Id());
    const bool assetChanged = generation != record.assetLoadGeneration;
    const bool nestedChanged = std::any_of(record.segments.begin(), record.segments.end(), [&](const auto& segment) {
        return scene.Assets().Manager().LoadGeneration(segment.source.asset.Id()) != segment.assetLoadGeneration ||
            std::any_of(segment.animations.begin(), segment.animations.end(), [&](const auto& animation) {
                return scene.Assets().Manager().LoadGeneration(animation.asset.Id()) != animation.assetLoadGeneration;
            });
    });
    const bool hierarchyChanged =
        record.observedHierarchyTopologyVersion !=
        state.hierarchyTopologyVersion;
    if (!assetChanged && !nestedChanged && !hierarchyChanged) return true;

    StopAudio(scene, record);
    auto previousBindings = record.bindings;
    if (assetChanged) {
        auto asset =
            scene.Assets().Manager().Load<TimelineAsset>(record.asset.Id());
        if (!asset.IsLoaded()) return false;
        record.asset = std::move(asset);
        record.assetLoadGeneration = generation;
        record.timeSeconds = std::clamp(
            record.timeSeconds, 0.0F, record.asset->durationSeconds);
        if (!record.looping && (record.playbackRate > 0.0F
            ? record.timeSeconds >= record.asset->durationSeconds : record.timeSeconds <= 0.0F)) {
            record.playing = false;
        }
    }
    if (assetChanged || nestedChanged) BuildSegments(scene, record);
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
    BuildSegments(scene, record);
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
    if (auto* record = Find(SceneAccess::State(scene), instance)) StopAudio(scene, *record);
    SceneAnimatorService::ReleaseTimelineClips(scene, instance);
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
    if (record == nullptr || (!record->looping && (record->playbackRate > 0.0F
        ? record->timeSeconds >= record->asset->durationSeconds : record->timeSeconds <= 0.0F))) {
        return false;
    }
    record->playing = true;
    return true;
}

bool SceneTimelineService::SetPlayback(Scene& scene, std::uint64_t instance, float rate, bool loop) noexcept {
    auto* record = Find(SceneAccess::State(scene), instance);
    if (record == nullptr || !std::isfinite(rate) || rate == 0.0F) return false;
    record->playbackRate = rate;
    record->looping = loop;
    if (rate < 0.0F) StopAudio(scene, *record);
    record->audioDiscontinuity = true;
    return true;
}

bool SceneTimelineService::Pause(
    Scene& scene, std::uint64_t instance) noexcept {
    TimelineRuntimeRecord* record =
        Find(SceneAccess::State(scene), instance);
    if (record == nullptr) return false;
    record->playing = false;
    for (auto& segment : record->segments) for (auto& voice : segment.audioVoices) {
        if (voice.id != 0 && !voice.paused) {
            if (kb::audio::AudioPlayback::PauseVoice(scene, voice.id)) voice.paused = true;
            else TimelineAudioPlayback::Stop(scene, voice);
        }
    }
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
    StopAudio(scene, *record);
    record->timeSeconds = timeSeconds;
    if (!record->looping && (record->playbackRate > 0.0F
        ? timeSeconds >= record->asset->durationSeconds : timeSeconds <= 0.0F)) {
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
        targetTimeSeconds < 0.0F ||
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
    StopAudio(scene, *record);
    record->timeSeconds = targetTimeSeconds;
    if (!record->looping && (record->playbackRate > 0.0F
        ? targetTimeSeconds >= record->asset->durationSeconds : targetTimeSeconds <= 0.0F)) {
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
    StopAudio(scene, *record);
    found->second =
        TimelineRuntimeBinding{ .target = target, .explicitlyBound = true };
    BindSegments(*record);
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
            SceneAnimatorService::ReleaseTimelineClips(scene, record.id);
            StopAudio(scene, record);
            iterator = state.timelines.erase(iterator);
            continue;
        }
        try {
        if (!Refresh(scene, record)) {
            StopAudio(scene, record);
            throw std::runtime_error(
                "Timeline asset reload or binding resolution failed");
        }
        if (record.playing && scene.Runtime().IsPlaying() &&
            deltaSeconds > 0.0F) {
            AdvanceCursor(state, record, deltaSeconds);
            if (!ApplyPose(scene, record)) {
                StopAudio(scene, record);
                throw std::runtime_error(
                    "Timeline lost an active Transform binding");
            }
        }
        SynchronizeAudio(scene, record);
        } catch (...) {
            SceneAnimatorService::ReleaseTimelineClips(scene, record.id);
            StopAudio(scene, record);
            record.playing = false;
            throw;
        }
        ++iterator;
    }
}

} // namespace kb::scene

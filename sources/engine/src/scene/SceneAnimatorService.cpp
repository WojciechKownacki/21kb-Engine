#include "scene/SceneAnimatorService.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/math/EngineMath.hpp"
#include "engine/scene/AnimationAssetIO.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAnimators.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace kb::scene {
namespace {

AnimatorRuntimeRecord* Find(SceneState& state, SceneEntity entity) {
    const auto it = state.animators.find(entity.Id());
    return it != state.animators.end() && it->second.entity == entity ? &it->second : nullptr;
}

const AnimatorRuntimeRecord* Find(const SceneState& state, SceneEntity entity) {
    const auto it = state.animators.find(entity.Id());
    return it != state.animators.end() && it->second.entity == entity ? &it->second : nullptr;
}

kb::assets::AssetId ResolveClip(kb::assets::AssetManager& manager, std::string_view reference) {
    kb::assets::AssetId id{};
    if (kb::assets::TryParseAssetId(reference, id) && id.IsValid()) {
        const auto* metadata = manager.Registry().Find(id);
        return metadata != nullptr && metadata->type == kAnimationClipAssetType ? id : kb::assets::AssetId{};
    }
    const auto* metadata = manager.Registry().FindByPath(std::filesystem::path{ reference });
    return metadata != nullptr && metadata->type == kAnimationClipAssetType ? metadata->id : kb::assets::AssetId{};
}

SceneEntity ResolveTarget(Scene& scene, SceneEntity owner, std::string_view path) {
    if (path.empty() || path == ".") return owner;
    SceneEntity current = owner;
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view segment = path.substr(0U, slash);
        if (segment.empty()) return {};
        SceneEntity found{};
        for (SceneEntity child : scene.Hierarchy().ChildEntities(current)) {
            if (scene.Entities().Name(child) == segment) {
                if (found.IsValid()) return {};
                found = child;
            }
        }
        if (!found.IsValid()) return {};
        current = found;
        if (slash == std::string_view::npos) break;
        path.remove_prefix(slash + 1U);
    }
    return current;
}

LocalTransform Sample(const AnimationTransformTrack& track, float time) {
    if (time <= track.keyframes.front().timeSeconds) return track.keyframes.front().transform;
    if (time >= track.keyframes.back().timeSeconds) return track.keyframes.back().transform;
    const auto upper = std::upper_bound(track.keyframes.begin(), track.keyframes.end(), time,
        [](float value, const AnimationTransformKeyframe& key) { return value < key.timeSeconds; });
    const auto& b = *upper;
    const auto& a = *(upper - 1);
    const float alpha = (time - a.timeSeconds) / (b.timeSeconds - a.timeSeconds);
    return LocalTransform{
        .position = {
            kb::math::Lerp(a.transform.position.x, b.transform.position.x, alpha),
            kb::math::Lerp(a.transform.position.y, b.transform.position.y, alpha),
            kb::math::Lerp(a.transform.position.z, b.transform.position.z, alpha) },
        .rotation = kb::math::Slerp(a.transform.rotation, b.transform.rotation, alpha),
        .scale = {
            kb::math::Lerp(a.transform.scale.x, b.transform.scale.x, alpha),
            kb::math::Lerp(a.transform.scale.y, b.transform.scale.y, alpha),
            kb::math::Lerp(a.transform.scale.z, b.transform.scale.z, alpha) },
    };
}

LocalTransform Blend(const LocalTransform& a, const LocalTransform& b, float weight) {
    return LocalTransform{
        .position = { kb::math::Lerp(a.position.x, b.position.x, weight), kb::math::Lerp(a.position.y, b.position.y, weight), kb::math::Lerp(a.position.z, b.position.z, weight) },
        .rotation = kb::math::Slerp(a.rotation, b.rotation, weight),
        .scale = { kb::math::Lerp(a.scale.x, b.scale.x, weight), kb::math::Lerp(a.scale.y, b.scale.y, weight), kb::math::Lerp(a.scale.z, b.scale.z, weight) },
    };
}

struct RootMotionDelta {
    Vec3 translation{};
    Quat rotation{};
};

[[nodiscard]] RootMotionDelta Compose(const RootMotionDelta& lhs, const RootMotionDelta& rhs) {
    return RootMotionDelta{
        .translation = lhs.translation + kb::math::Rotate(lhs.rotation, rhs.translation),
        .rotation = kb::math::Normalize(lhs.rotation * rhs.rotation),
    };
}

[[nodiscard]] RootMotionDelta Inverse(const RootMotionDelta& value) {
    const Quat inverseRotation = kb::math::Inverse(value.rotation);
    return RootMotionDelta{
        .translation = kb::math::Rotate(inverseRotation, value.translation * -1.0F),
        .rotation = inverseRotation,
    };
}

[[nodiscard]] RootMotionDelta Relative(const RootMotionDelta& from, const RootMotionDelta& to) {
    return Compose(Inverse(from), to);
}

[[nodiscard]] RootMotionDelta Pow(RootMotionDelta value, std::size_t exponent) {
    RootMotionDelta result{};
    while (exponent > 0U) {
        if ((exponent & 1U) != 0U) result = Compose(result, value);
        exponent >>= 1U;
        if (exponent > 0U) value = Compose(value, value);
    }
    return result;
}

[[nodiscard]] RootMotionDelta RootPose(const AnimationTransformTrack& track, float timeSeconds) {
    const LocalTransform pose = Sample(track, timeSeconds);
    return RootMotionDelta{ .translation = pose.position, .rotation = pose.rotation };
}

[[nodiscard]] const AnimationTransformTrack* RootTrack(
    const AnimatorRuntimeState& state,
    const AnimatorControllerLayer& layer) {
    const AnimationClip& clip = *state.clip;
    for (const AnimationTransformTrack& track : clip.tracks) {
        if (track.targetPath.empty() && (track.bindingMask & layer.mask) != 0U) return &track;
    }
    return nullptr;
}

[[nodiscard]] RootMotionDelta StateRootMotion(
    const AnimatorRuntimeState& state,
    const AnimatorControllerLayer& layer,
    double startSeconds,
    double deltaSeconds) {
    const AnimationTransformTrack* track = RootTrack(state, layer);
    if (track == nullptr) {
        throw std::runtime_error("Root motion requires an owner track on the controller's first layer");
    }
    const AnimationClip& clip = *state.clip;
    const RootMotionDelta start = RootPose(*track, static_cast<float>(startSeconds));
    if (!clip.looping) {
        const float end = static_cast<float>(std::min(
            startSeconds + deltaSeconds, static_cast<double>(clip.durationSeconds)));
        return Relative(start, RootPose(*track, end));
    }

    const double duration = static_cast<double>(clip.durationSeconds);
    const double unwrappedEnd = startSeconds + deltaSeconds;
    const std::size_t cycles = static_cast<std::size_t>(std::floor(unwrappedEnd / duration));
    double wrappedEnd = std::fmod(unwrappedEnd, duration);
    if (wrappedEnd < 0.0) wrappedEnd += duration;
    const RootMotionDelta first = RootPose(*track, 0.0F);
    const RootMotionDelta cycle = Relative(first, RootPose(*track, clip.durationSeconds));
    const RootMotionDelta endFromFirst = Relative(first, RootPose(*track, static_cast<float>(wrappedEnd)));
    return Relative(Relative(first, start), Compose(Pow(cycle, cycles), endFromFirst));
}

[[nodiscard]] RootMotionDelta ExtractRootMotion(
    const AnimatorRuntimeRecord& record,
    double scaledDelta,
    double frameDelta) {
    if (scaledDelta <= 0.0 || record.layers.empty()) return {};
    const AnimatorControllerLayer& definition = record.controller->layers.front();
    const AnimatorRuntimeLayer& layer = record.layers.front();
    RootMotionDelta delta = StateRootMotion(
        layer.states[layer.currentState], definition, layer.currentTimeSeconds, scaledDelta);
    if (layer.transitioning) {
        const RootMotionDelta previous = StateRootMotion(
            layer.states[layer.previousState], definition, layer.previousTimeSeconds, scaledDelta);
        const float transition = static_cast<float>(std::clamp(
            (layer.transitionElapsedSeconds + frameDelta) / layer.transitionDurationSeconds, 0.0, 1.0));
        delta.translation = previous.translation +
            (delta.translation - previous.translation) * transition;
        delta.rotation = kb::math::Slerp(previous.rotation, delta.rotation, transition);
    }
    delta.translation = delta.translation * definition.weight;
    delta.rotation = kb::math::Slerp(Quat{}, delta.rotation, definition.weight);
    return delta;
}

std::size_t StateIndex(const AnimatorControllerLayer& layer, std::string_view name) {
    const auto it = std::find_if(layer.states.begin(), layer.states.end(), [name](const AnimatorControllerState& state) { return state.name == name; });
    return it == layer.states.end() ? layer.states.size() : static_cast<std::size_t>(it - layer.states.begin());
}

std::size_t LayerIndex(const AnimatorController& controller, std::string_view name) {
    const auto it = std::find_if(controller.layers.begin(), controller.layers.end(), [name](const AnimatorControllerLayer& layer) { return layer.name == name; });
    return it == controller.layers.end() ? controller.layers.size() : static_cast<std::size_t>(it - controller.layers.begin());
}

double StateTime(float normalizedTime, const AnimationClip& clip) {
    return static_cast<double>(std::clamp(normalizedTime, 0.0F, 1.0F)) * static_cast<double>(clip.durationSeconds);
}

void AdvanceTime(double& time, double delta, const AnimationClip& clip) {
    time += delta;
    const double duration = static_cast<double>(clip.durationSeconds);
    if (clip.looping) {
        time = std::fmod(time, duration);
        if (time < 0.0) time += duration;
    } else {
        time = std::clamp(time, 0.0, duration);
    }
}

std::size_t EventCrossingCount(const AnimatorRuntimeState& state, double start, double delta, std::size_t limit) {
    if (delta <= 0.0 || state.clip->events.empty()) return 0U;
    const double duration = static_cast<double>(state.clip->durationSeconds);
    std::size_t total = 0U;
    for (const AnimationEventKeyframe& event : state.clip->events) {
        std::size_t count = 0U;
        if (state.clip->looping) {
            double first = static_cast<double>(event.timeSeconds) - start;
            if (first <= 0.0) first += duration;
            if (first <= delta) {
                const double occurrences = std::floor((delta - first) / duration) + 1.0;
                if (occurrences > static_cast<double>(limit - total)) return limit + 1U;
                count = static_cast<std::size_t>(occurrences);
            }
        } else if (static_cast<double>(event.timeSeconds) > start &&
                   static_cast<double>(event.timeSeconds) <= std::min(start + delta, duration)) {
            count = 1U;
        }
        if (count > limit - total) return limit + 1U;
        total += count;
    }
    return total;
}

void QueueStateEvents(
    const AnimatorRuntimeState& state,
    double start,
    double delta,
    SceneEntity target,
    std::string_view layerName,
    std::string_view stateName,
    std::vector<AnimationEventRecord>& pending) {
    if (delta <= 0.0 || state.clip->events.empty()) return;
    const double duration = static_cast<double>(state.clip->durationSeconds);
    const auto append = [&](const AnimationEventKeyframe& event) {
        pending.push_back(AnimationEventRecord{
            .target = target,
            .eventId = event.id,
            .clipAssetId = state.clip.Id().value,
            .layer = std::string{ layerName },
            .state = std::string{ stateName },
            .normalizedTime = event.timeSeconds / state.clip->durationSeconds,
        });
    };
    if (!state.clip->looping) {
        const double end = std::min(start + delta, duration);
        for (const AnimationEventKeyframe& event : state.clip->events) {
            if (static_cast<double>(event.timeSeconds) > start && static_cast<double>(event.timeSeconds) <= end) append(event);
        }
        return;
    }

    const double end = start + delta;
    const std::size_t lastCycle = static_cast<std::size_t>(std::floor(end / duration));
    for (std::size_t cycle = 0U; cycle <= lastCycle; ++cycle) {
        const double cycleOffset = static_cast<double>(cycle) * duration;
        for (const AnimationEventKeyframe& event : state.clip->events) {
            const double occurrence = cycleOffset + static_cast<double>(event.timeSeconds);
            if (occurrence > start && occurrence <= end) append(event);
        }
    }
}

template <typename Setter>
bool SetParameter(Scene& scene, SceneEntity entity, std::string_view name, AnimatorParameterType type, Setter&& setter) {
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return false;
    for (std::size_t index = 0U; index < record->controller->parameters.size(); ++index) {
        const AnimatorParameterDefinition& definition = record->controller->parameters[index];
        if (definition.name == name && definition.type == type) {
            setter(record->parameters[index]);
            return true;
        }
    }
    return false;
}

void EvaluateState(
    AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    double time,
    std::uint64_t mask,
    bool from) {
    const AnimationClip& clip = *state.clip;
    const float sampleTime = static_cast<float>(time);
    for (std::size_t trackIndex = 0U; trackIndex < clip.tracks.size(); ++trackIndex) {
        const AnimationTransformTrack& track = clip.tracks[trackIndex];
        if ((track.bindingMask & mask) == 0U) continue;
        AnimatorRuntimeBinding& binding = record.bindings[state.targetIndices[trackIndex]];
        if (from) {
            binding.fromPose = Sample(track, sampleTime);
            binding.fromTouched = true;
        } else {
            binding.toPose = Sample(track, sampleTime);
            binding.toTouched = true;
        }
    }
}

bool ConditionMatches(
    const AnimatorRuntimeRecord& record,
    const AnimatorTransitionCondition& condition) {
    for (std::size_t index = 0U; index < record.controller->parameters.size(); ++index) {
        if (record.controller->parameters[index].name != condition.parameter) continue;
        const AnimatorParameterValue& value = record.parameters[index];
        switch (condition.mode) {
        case AnimatorConditionMode::BoolEquals: return value.boolValue == condition.boolValue;
        case AnimatorConditionMode::IntEquals: return value.intValue == condition.intValue;
        case AnimatorConditionMode::IntGreater: return value.intValue > condition.intValue;
        case AnimatorConditionMode::IntLess: return value.intValue < condition.intValue;
        case AnimatorConditionMode::FloatGreater: return value.floatValue > condition.floatValue;
        case AnimatorConditionMode::FloatLess: return value.floatValue < condition.floatValue;
        case AnimatorConditionMode::TriggerSet: return value.boolValue;
        }
    }
    return false;
}

void ConsumeTransitionTriggers(
    AnimatorRuntimeRecord& record,
    const AnimatorControllerTransition& transition) {
    for (const AnimatorTransitionCondition& condition : transition.conditions) {
        if (condition.mode != AnimatorConditionMode::TriggerSet) continue;
        for (std::size_t index = 0U; index < record.controller->parameters.size(); ++index) {
            if (record.controller->parameters[index].name == condition.parameter) {
                record.parameters[index].boolValue = false;
                break;
            }
        }
    }
}

bool StartTransition(
    AnimatorRuntimeRecord& record,
    std::size_t layerIndex,
    std::size_t stateIndex,
    float durationSeconds,
    float normalizedTime) {
    AnimatorRuntimeLayer& layer = record.layers[layerIndex];
    if (layer.transitioning || stateIndex >= layer.states.size()) return false;
    layer.previousState = layer.currentState;
    layer.previousTimeSeconds = layer.currentTimeSeconds;
    layer.currentState = stateIndex;
    layer.currentTimeSeconds = StateTime(normalizedTime, *layer.states[stateIndex].clip);
    AdvanceTime(layer.currentTimeSeconds, 0.0, *layer.states[stateIndex].clip);
    layer.transitionElapsedSeconds = 0.0;
    layer.transitionDurationSeconds = durationSeconds;
    layer.transitioning = durationSeconds > 0.0F;
    if (!layer.transitioning) {
        layer.previousState = layer.currentState;
        layer.previousTimeSeconds = layer.currentTimeSeconds;
    }
    return true;
}

void EvaluateControllerTransitions(AnimatorRuntimeRecord& record) {
    for (std::size_t layerIndex = 0U; layerIndex < record.layers.size(); ++layerIndex) {
        AnimatorRuntimeLayer& layer = record.layers[layerIndex];
        if (layer.transitioning) continue;
        const AnimatorControllerLayer& definition = record.controller->layers[layerIndex];
        const std::string& currentState = definition.states[layer.currentState].name;
        const float duration = layer.states[layer.currentState].clip->durationSeconds;
        const float normalizedTime = duration > 0.0F
            ? static_cast<float>(layer.currentTimeSeconds / static_cast<double>(duration))
            : 0.0F;
        for (const AnimatorControllerTransition& transition : definition.transitions) {
            if (transition.fromState != currentState ||
                (transition.exitNormalizedTime >= 0.0F && normalizedTime < transition.exitNormalizedTime) ||
                !std::all_of(transition.conditions.begin(), transition.conditions.end(),
                    [&](const AnimatorTransitionCondition& condition) { return ConditionMatches(record, condition); })) {
                continue;
            }
            const std::size_t target = StateIndex(definition, transition.toState);
            if (StartTransition(record, layerIndex, target, transition.durationSeconds, 0.0F)) {
                ConsumeTransitionTriggers(record, transition);
            }
            break;
        }
    }
}

} // namespace

bool SceneAnimatorService::Attach(Scene& scene, SceneEntity entity, std::uint64_t controllerAssetId) {
    if (!scene.Entities().IsAlive(entity)) return false;
    auto& manager = scene.Assets().Manager();
    auto controller = manager.Load<AnimatorController>(kb::assets::AssetId{ controllerAssetId });
    if (!controller.IsLoaded()) return false;

    AnimatorRuntimeRecord candidate{};
    candidate.entity = entity;
    candidate.controller = controller;
    candidate.parameters.reserve(controller->parameters.size());
    for (const auto& definition : controller->parameters) {
        candidate.parameters.push_back(AnimatorParameterValue{
            .type = definition.type,
            .boolValue = definition.boolDefault,
            .intValue = definition.intDefault,
            .floatValue = definition.floatDefault,
        });
    }
    candidate.layers.reserve(controller->layers.size());
    for (const AnimatorControllerLayer& layerDefinition : controller->layers) {
        AnimatorRuntimeLayer layer{};
        layer.states.reserve(layerDefinition.states.size());
        for (const AnimatorControllerState& stateDefinition : layerDefinition.states) {
            auto clip = manager.Load<AnimationClip>(ResolveClip(manager, stateDefinition.clipReference));
            if (!clip.IsLoaded()) return false;
            AnimatorRuntimeState state{};
            state.clip = std::move(clip);
            state.targetIndices.reserve(state.clip->tracks.size());
            for (const AnimationTransformTrack& track : state.clip->tracks) {
                const SceneEntity target = ResolveTarget(scene, entity, track.targetPath);
                const TransformComponent* transform = scene.Transforms().TryGet(target);
                if (!target.IsValid() || transform == nullptr) return false;
                auto binding = std::find_if(candidate.bindings.begin(), candidate.bindings.end(),
                    [target](const AnimatorRuntimeBinding& value) { return value.target == target; });
                if (binding == candidate.bindings.end()) {
                    candidate.bindings.push_back(AnimatorRuntimeBinding{
                        .target = target,
                        .bindTransform = transform->LocalPayload(),
                    });
                    binding = candidate.bindings.end() - 1;
                }
                state.targetIndices.push_back(static_cast<std::size_t>(binding - candidate.bindings.begin()));
            }
            layer.states.push_back(std::move(state));
        }
        layer.currentState = StateIndex(layerDefinition, layerDefinition.defaultState);
        if (layer.currentState >= layer.states.size()) return false;
        layer.previousState = layer.currentState;
        candidate.layers.push_back(std::move(layer));
    }
    SceneAccess::State(scene).animators[entity.Id()] = std::move(candidate);
    return true;
}

bool SceneAnimatorService::Exists(const Scene& scene, SceneEntity entity) noexcept {
    return Find(SceneAccess::State(scene), entity) != nullptr;
}

std::uint64_t SceneAnimatorService::Controller(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record == nullptr ? 0U : record->controller.Id().value;
}

std::span<const AnimatorParameterValue> SceneAnimatorService::Parameters(const Scene& scene, SceneEntity entity) noexcept {
    const auto* record = Find(SceneAccess::State(scene), entity);
    return record == nullptr ? std::span<const AnimatorParameterValue>{} : std::span<const AnimatorParameterValue>{ record->parameters };
}

bool SceneAnimatorService::Play(Scene& scene, SceneEntity entity, std::string_view layerName, std::string_view stateName, float normalizedTime) noexcept {
    if (!std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F) return false;
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return false;
    const std::size_t layerIndex = LayerIndex(*record->controller, layerName);
    if (layerIndex >= record->layers.size()) return false;
    const std::size_t stateIndex = StateIndex(record->controller->layers[layerIndex], stateName);
    if (stateIndex >= record->layers[layerIndex].states.size()) return false;
    AnimatorRuntimeLayer& layer = record->layers[layerIndex];
    layer.currentState = stateIndex;
    layer.previousState = stateIndex;
    layer.currentTimeSeconds = StateTime(normalizedTime, *layer.states[stateIndex].clip);
    AdvanceTime(layer.currentTimeSeconds, 0.0, *layer.states[stateIndex].clip);
    layer.previousTimeSeconds = layer.currentTimeSeconds;
    layer.transitioning = false;
    layer.transitionElapsedSeconds = 0.0;
    layer.transitionDurationSeconds = 0.0;
    return true;
}

bool SceneAnimatorService::CrossFade(
    Scene& scene, SceneEntity entity, std::string_view layerName, std::string_view stateName,
    float durationSeconds, float normalizedTime) noexcept {
    if (!std::isfinite(durationSeconds) || durationSeconds < 0.0F ||
        !std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F) return false;
    if (durationSeconds == 0.0F) return Play(scene, entity, layerName, stateName, normalizedTime);
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return false;
    const std::size_t layerIndex = LayerIndex(*record->controller, layerName);
    if (layerIndex >= record->layers.size()) return false;
    const std::size_t stateIndex = StateIndex(record->controller->layers[layerIndex], stateName);
    if (stateIndex >= record->layers[layerIndex].states.size()) return false;
    return StartTransition(*record, layerIndex, stateIndex, durationSeconds, normalizedTime);
}

bool SceneAnimatorService::SetSpeed(Scene& scene, SceneEntity entity, float speed) noexcept {
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr || !std::isfinite(speed) || speed < 0.0F) return false;
    record->speed = speed;
    return true;
}

float SceneAnimatorService::Speed(const Scene& scene, SceneEntity entity) noexcept {
    const AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    return record == nullptr ? 0.0F : record->speed;
}

bool SceneAnimatorService::SetBool(Scene& scene, SceneEntity entity, std::string_view name, bool value) noexcept {
    return SetParameter(scene, entity, name, AnimatorParameterType::Bool, [value](AnimatorParameterValue& parameter) { parameter.boolValue = value; });
}

bool SceneAnimatorService::SetInt(Scene& scene, SceneEntity entity, std::string_view name, std::int32_t value) noexcept {
    return SetParameter(scene, entity, name, AnimatorParameterType::Int, [value](AnimatorParameterValue& parameter) { parameter.intValue = value; });
}

bool SceneAnimatorService::SetFloat(Scene& scene, SceneEntity entity, std::string_view name, float value) noexcept {
    if (!std::isfinite(value)) return false;
    return SetParameter(scene, entity, name, AnimatorParameterType::Float, [value](AnimatorParameterValue& parameter) { parameter.floatValue = value; });
}

bool SceneAnimatorService::SetTrigger(Scene& scene, SceneEntity entity, std::string_view name, bool value) noexcept {
    return SetParameter(scene, entity, name, AnimatorParameterType::Trigger, [value](AnimatorParameterValue& parameter) { parameter.boolValue = value; });
}

std::optional<AnimatorStateInfo> SceneAnimatorService::State(const Scene& scene, SceneEntity entity, std::string_view layerName) {
    const AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    const std::size_t layerIndex = LayerIndex(*record->controller, layerName);
    if (layerIndex >= record->layers.size()) return std::nullopt;
    const AnimatorRuntimeLayer& layer = record->layers[layerIndex];
    const AnimatorControllerLayer& definition = record->controller->layers[layerIndex];
    const float duration = layer.states[layer.currentState].clip->durationSeconds;
    return AnimatorStateInfo{
        .state = definition.states[layer.currentState].name,
        .previousState = definition.states[layer.previousState].name,
        .normalizedTime = duration > 0.0F ? static_cast<float>(layer.currentTimeSeconds / duration) : 0.0F,
        .transitionProgress = layer.transitioning
            ? static_cast<float>(std::clamp(layer.transitionElapsedSeconds / layer.transitionDurationSeconds, 0.0, 1.0))
            : 1.0F,
        .transitioning = layer.transitioning,
    };
}

std::vector<AnimationEventRecord> SceneAnimatorService::DrainEvents(Scene& scene) {
    std::vector<AnimationEventRecord> drained;
    drained.swap(SceneAccess::State(scene).pendingAnimationEvents);
    return drained;
}

void SceneAnimatorService::SyncComponents(Scene& scene) {
    if (scene.Entities().Count() == 0U) {
        SceneState& state = SceneAccess::State(scene);
        state.animators.clear();
        state.pendingAnimationEvents.clear();
        return;
    }
    struct AuthoredAnimator {
        SceneEntity entity{};
        Animator animator{};
    };
    std::vector<AuthoredAnimator> authored;
    std::vector<SceneEntity> pending = scene.Hierarchy().RootEntities();
    while (!pending.empty()) {
        const SceneEntity entity = pending.back();
        pending.pop_back();
        const auto children = scene.Hierarchy().ChildEntities(entity);
        pending.insert(pending.end(), children.begin(), children.end());
        if (const Animator* animator = SceneAccess::State(scene).componentStorage.Animators().TryGet(entity)) {
            authored.push_back(AuthoredAnimator{ .entity = entity, .animator = *animator });
        }
    }

    std::map<std::uint64_t, bool> retained;
    for (const AuthoredAnimator& value : authored) {
        if (!value.animator.enabled) continue;
        if (value.animator.controllerAssetId == 0U || !std::isfinite(value.animator.speed) || value.animator.speed < 0.0F) {
            throw std::runtime_error("Enabled Animator component has an invalid controller or speed");
        }
        retained[value.entity.Id()] = true;
        AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), value.entity);
        if (record == nullptr || record->controller.Id().value != value.animator.controllerAssetId) {
            if (!Attach(scene, value.entity, value.animator.controllerAssetId)) {
                throw std::runtime_error("Animator component could not load its controller or bind the authored hierarchy");
            }
            record = Find(SceneAccess::State(scene), value.entity);
            record->lastAppliedComponentSpeed = value.animator.speed;
            record->speed = value.animator.speed;
        }
        if (record->lastAppliedComponentSpeed != value.animator.speed) {
            record->lastAppliedComponentSpeed = value.animator.speed;
            record->speed = value.animator.speed;
        }
    }
    SceneState& state = SceneAccess::State(scene);
    for (auto it = state.animators.begin(); it != state.animators.end();) {
        const Animator* component = state.componentStorage.Animators().TryGet(it->second.entity);
        if (component != nullptr && (!component->enabled || !retained.contains(it->first))) {
            it = state.animators.erase(it);
        } else {
            ++it;
        }
    }
}

void SceneAnimatorService::Advance(Scene& scene, float deltaSeconds) {
    SceneState& state = SceneAccess::State(scene);
    for (auto it = state.animators.begin(); it != state.animators.end();) {
        if (!scene.Entities().IsAlive(it->second.entity)) it = state.animators.erase(it);
        else ++it;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        throw std::invalid_argument("AnimatorSceneSystem requires a finite, non-negative deltaSeconds");
    }
    if (!state.isPlaying) return;

    for (auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (scene.Entities().IsActive(record.entity)) EvaluateControllerTransitions(record);
    }

    if (state.pendingAnimationEvents.size() > SceneAnimators::kMaxPendingEvents) {
        throw std::length_error("AnimatorSceneSystem pending animation-event queue exceeded its capacity");
    }
    std::size_t eventCount = state.pendingAnimationEvents.size();
    for (const auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (!scene.Entities().IsActive(record.entity)) continue;
        const double scaledDelta = static_cast<double>(deltaSeconds) * static_cast<double>(record.speed);
        for (const AnimatorRuntimeLayer& layer : record.layers) {
            const std::size_t available = SceneAnimators::kMaxPendingEvents - eventCount;
            const std::size_t currentCount = EventCrossingCount(layer.states[layer.currentState], layer.currentTimeSeconds, scaledDelta, available);
            if (currentCount > available) {
                throw std::length_error("AnimatorSceneSystem exceeded the pending animation-event capacity");
            }
            eventCount += currentCount;
            if (layer.transitioning) {
                const std::size_t remaining = SceneAnimators::kMaxPendingEvents - eventCount;
                const std::size_t previousCount = EventCrossingCount(layer.states[layer.previousState], layer.previousTimeSeconds, scaledDelta, remaining);
                if (previousCount > remaining) {
                    throw std::length_error("AnimatorSceneSystem exceeded the pending animation-event capacity");
                }
                eventCount += previousCount;
            }
        }
    }
    state.pendingAnimationEvents.reserve(eventCount);

    for (const auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (!scene.Entities().IsActive(record.entity)) continue;
        const Animator* component = scene.Components().Animators().TryGet(record.entity);
        if (component == nullptr || component->rootMotionOwner == AnimatorRootMotionOwner::None) continue;
        const double scaledDelta = static_cast<double>(deltaSeconds) * static_cast<double>(record.speed);
        const RootMotionDelta motion = ExtractRootMotion(record, scaledDelta, deltaSeconds);
        switch (component->rootMotionOwner) {
        case AnimatorRootMotionOwner::None:
            break;
        case AnimatorRootMotionOwner::Animator: {
            const RigidbodyComponent* rigidbody =
                scene.Components().Rigidbodies().TryGet(record.entity);
            if (scene.Components().CharacterControllers().TryGet(record.entity) != nullptr ||
                (rigidbody != nullptr &&
                 rigidbody->bodyType != RigidbodyBodyType::Static &&
                 scene.Components().Colliders().TryGet(record.entity) != nullptr)) {
                throw std::runtime_error(
                    "Animator-owned root motion cannot share Transform authority with a CharacterController or simulated Rigidbody");
            }
            break;
        }
        case AnimatorRootMotionOwner::CharacterController:
            if (const TransformComponent* transform = scene.Transforms().TryGet(record.entity);
                transform == nullptr ||
                std::abs(kb::math::Rotate(transform->worldRotation, motion.translation).y) > 1.0e-5F) {
                throw std::runtime_error(
                    "CharacterController root motion must be world-planar; vertical motion remains owned by gravity and CharacterJump");
            }
            if (scene.Components().CharacterControllers().TryGet(record.entity) == nullptr ||
                scene.Components().Rigidbodies().TryGet(record.entity) != nullptr ||
                scene.Components().Colliders().TryGet(record.entity) != nullptr ||
                !PhysicsBackend::QueueCharacterRootMotion(
                    scene, record.entity, motion.translation, motion.rotation, deltaSeconds)) {
                throw std::runtime_error(
                    "Animator root motion owner CharacterController requires an exclusive CharacterController and live physics backend");
            }
            break;
        case AnimatorRootMotionOwner::Rigidbody: {
            const RigidbodyComponent* rigidbody = scene.Components().Rigidbodies().TryGet(record.entity);
            if (rigidbody == nullptr || rigidbody->bodyType != RigidbodyBodyType::Kinematic ||
                scene.Components().Colliders().TryGet(record.entity) == nullptr ||
                scene.Components().CharacterControllers().TryGet(record.entity) != nullptr ||
                !PhysicsBackend::QueueRigidbodyRootMotion(
                    scene, record.entity, motion.translation, motion.rotation, deltaSeconds)) {
                throw std::runtime_error(
                    "Animator root motion owner Rigidbody requires an exclusive kinematic Rigidbody, Collider, and live physics backend");
            }
            break;
        }
        default:
            throw std::runtime_error("Animator component contains an invalid root-motion owner");
        }
    }

    for (auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (!scene.Entities().IsActive(record.entity)) continue;
        const Animator* component = scene.Components().Animators().TryGet(record.entity);
        const AnimatorRootMotionOwner rootMotionOwner = component == nullptr
            ? AnimatorRootMotionOwner::None
            : component->rootMotionOwner;
        const RootMotionDelta animatorMotion =
            rootMotionOwner == AnimatorRootMotionOwner::Animator
                ? ExtractRootMotion(
                      record,
                      static_cast<double>(deltaSeconds) * static_cast<double>(record.speed),
                      deltaSeconds)
                : RootMotionDelta{};
        for (AnimatorRuntimeBinding& binding : record.bindings) {
            binding.output = binding.bindTransform;
            binding.outputTouched = false;
        }
        const double scaledDelta = static_cast<double>(deltaSeconds) * static_cast<double>(record.speed);
        for (std::size_t layerIndex = 0U; layerIndex < record.layers.size(); ++layerIndex) {
            AnimatorRuntimeLayer& layer = record.layers[layerIndex];
            const AnimatorControllerLayer& definition = record.controller->layers[layerIndex];
            if (layer.transitioning) {
                QueueStateEvents(
                    layer.states[layer.previousState], layer.previousTimeSeconds, scaledDelta, record.entity,
                    definition.name, definition.states[layer.previousState].name, state.pendingAnimationEvents);
            }
            QueueStateEvents(
                layer.states[layer.currentState], layer.currentTimeSeconds, scaledDelta, record.entity,
                definition.name, definition.states[layer.currentState].name, state.pendingAnimationEvents);
            AdvanceTime(layer.currentTimeSeconds, scaledDelta, *layer.states[layer.currentState].clip);
            if (layer.transitioning) {
                AdvanceTime(layer.previousTimeSeconds, scaledDelta, *layer.states[layer.previousState].clip);
                layer.transitionElapsedSeconds = std::min(layer.transitionElapsedSeconds + static_cast<double>(deltaSeconds), layer.transitionDurationSeconds);
                for (AnimatorRuntimeBinding& binding : record.bindings) {
                    binding.fromTouched = false;
                    binding.toTouched = false;
                }
                EvaluateState(record, layer.states[layer.previousState], layer.previousTimeSeconds, definition.mask, true);
                EvaluateState(record, layer.states[layer.currentState], layer.currentTimeSeconds, definition.mask, false);
                const float transition = static_cast<float>(layer.transitionElapsedSeconds / layer.transitionDurationSeconds);
                for (AnimatorRuntimeBinding& binding : record.bindings) {
                    if (!binding.fromTouched && !binding.toTouched) continue;
                    const LocalTransform from = binding.fromTouched ? binding.fromPose : binding.bindTransform;
                    const LocalTransform to = binding.toTouched ? binding.toPose : binding.bindTransform;
                    binding.output = Blend(binding.output, Blend(from, to, transition), definition.weight);
                    binding.outputTouched = true;
                }
                if (layer.transitionElapsedSeconds >= layer.transitionDurationSeconds) {
                    layer.transitioning = false;
                    layer.previousState = layer.currentState;
                    layer.previousTimeSeconds = layer.currentTimeSeconds;
                }
            } else {
                const AnimatorRuntimeState& runtimeState = layer.states[layer.currentState];
                const AnimationClip& clip = *runtimeState.clip;
                for (std::size_t trackIndex = 0U; trackIndex < clip.tracks.size(); ++trackIndex) {
                    const AnimationTransformTrack& track = clip.tracks[trackIndex];
                    if ((track.bindingMask & definition.mask) == 0U) continue;
                    AnimatorRuntimeBinding& binding = record.bindings[runtimeState.targetIndices[trackIndex]];
                    binding.output = Blend(binding.output, Sample(track, static_cast<float>(layer.currentTimeSeconds)), definition.weight);
                    binding.outputTouched = true;
                }
            }
        }
        for (AnimatorRuntimeBinding& binding : record.bindings) {
            if (!binding.outputTouched || !scene.Entities().IsAlive(binding.target)) continue;
            TransformComponent* transform = scene.Transforms().TryGet(binding.target);
            if (transform == nullptr) continue;
            if (rootMotionOwner == AnimatorRootMotionOwner::None || binding.target != record.entity) {
                transform->localPosition = binding.output.position;
                transform->localRotation = binding.output.rotation;
            }
            transform->localScale = binding.output.scale;
            scene.Transforms().MarkModified(binding.target);
        }
        if (rootMotionOwner == AnimatorRootMotionOwner::Animator) {
            TransformComponent* transform = scene.Transforms().TryGet(record.entity);
            if (transform == nullptr) {
                throw std::runtime_error("Animator root motion owner has no Transform component");
            }
            transform->localPosition = transform->localPosition +
                kb::math::Rotate(transform->localRotation, animatorMotion.translation);
            transform->localRotation = kb::math::Normalize(
                transform->localRotation * animatorMotion.rotation);
            scene.Transforms().MarkModified(record.entity);
        }
    }
}

} // namespace kb::scene

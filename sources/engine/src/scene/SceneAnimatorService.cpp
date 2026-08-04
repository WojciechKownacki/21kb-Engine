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
#include "engine/scene/SkeletonBindingComponent.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
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

[[nodiscard]] const AnimationClip& ReferenceClip(
    const AnimatorRuntimeState& state);
double StateTime(float normalizedTime, const AnimationClip& clip);

[[nodiscard]] bool RuntimeAssetsAreCurrent(
    const kb::assets::AssetManager& manager,
    const AnimatorRuntimeRecord& record) {
    if (record.skeleton.has_value() &&
        record.skeleton->loadGeneration !=
            manager.LoadGeneration(record.skeleton->asset.Id())) {
        return false;
    }
    for (const AnimatorRuntimeLayer& runtimeLayer : record.layers) {
        for (const AnimatorRuntimeState& runtimeState : runtimeLayer.states) {
            for (const AnimatorRuntimeState::Motion& motion :
                 runtimeState.motions) {
                if (motion.clipLoadGeneration !=
                    manager.LoadGeneration(motion.clip.Id())) {
                    return false;
                }
            }
        }
    }
    return true;
}

void BuildAndPublishDebugSnapshot(Scene& scene, SceneState& state) {
    AnimatorDebugSnapshot published{};
    published.revision = ++state.animatorDebugSnapshotRevision;
    published.instances.reserve(state.animators.size());
    std::erase_if(state.animatorDebugRootMotionTrails,
        [&state](const auto& value) { return !state.animators.contains(value.first); });
    for (const auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (!scene.Entities().IsAlive(record.entity) || !record.controller.IsLoaded()) {
            continue;
        }
        AnimatorDebugInstanceSnapshot instance{};
        instance.entity = record.entity;
        instance.controllerAssetId = record.controller.Id().value;
        instance.runtimeBindingGeneration = record.runtimeBindingGeneration;
        instance.parameters.reserve(record.parameters.size());
        for (std::size_t index = 0U; index < record.parameters.size(); ++index) {
            const AnimatorParameterValue& value = record.parameters[index];
            const AnimatorParameterDefinition& definition = record.controller->parameters[index];
            instance.parameters.push_back({
                .name = definition.name,
                .type = value.type,
                .boolValue = value.boolValue,
                .intValue = value.intValue,
                .floatValue = value.floatValue,
            });
        }
        instance.layers.reserve(record.layers.size());
        for (std::size_t index = 0U; index < record.layers.size(); ++index) {
            const AnimatorRuntimeLayer& runtime = record.layers[index];
            const AnimatorControllerLayer& definition = record.controller->layers[index];
            const AnimatorControllerState& active = definition.states[runtime.currentState];
            const AnimatorControllerState& previous = definition.states[runtime.previousState];
            const AnimationClip& clip = ReferenceClip(runtime.states[runtime.currentState]);
            std::uint64_t transitionId = 0U;
            if (runtime.transitioning) {
                const auto transition = std::find_if(
                    definition.transitions.begin(), definition.transitions.end(),
                    [&previous, &active](const AnimatorControllerTransition& candidate) {
                        return candidate.fromState == previous.name && candidate.toState == active.name;
                    });
                if (transition != definition.transitions.end()) transitionId = transition->id;
            }
            instance.layers.push_back({
                .name = definition.name,
                .activeStateId = active.id,
                .previousStateId = previous.id,
                .activeTransitionId = transitionId,
                .activeState = active.name,
                .previousState = previous.name,
                .normalizedTime = clip.durationSeconds > 0.0F
                    ? static_cast<float>(runtime.currentTimeSeconds / clip.durationSeconds) : 0.0F,
                .transitionProgress = runtime.transitioning && runtime.transitionDurationSeconds > 0.0
                    ? static_cast<float>(std::clamp(runtime.transitionElapsedSeconds / runtime.transitionDurationSeconds, 0.0, 1.0)) : 1.0F,
                .weight = definition.weight,
                .elapsedSeconds = runtime.currentTimeSeconds,
                .transitioning = runtime.transitioning,
            });
        }
        instance.constraints.reserve(record.rigConstraints.size());
        for (const AnimatorRuntimeConstraint& runtime : record.rigConstraints) {
            const AnimatorRigConstraint& definition = record.controller->rigConstraints[runtime.definitionIndex];
            AnimatorDebugConstraintSnapshot constraint{};
            constraint.name = definition.name;
            constraint.type = definition.type;
            constraint.constrainedBoneId = definition.constrainedBoneId;
            constraint.midBoneId = definition.midBoneId;
            constraint.tipBoneId = definition.tipBoneId;
            constraint.target = definition.target;
            constraint.poleTarget = definition.poleTarget;
            if (const auto target = record.ikTargets.find(definition.target); target != record.ikTargets.end()) {
                constraint.targetValue = target->second;
                constraint.hasTarget = true;
            }
            if (const auto target = record.ikTargets.find(definition.poleTarget); target != record.ikTargets.end()) {
                constraint.poleTargetValue = target->second;
                constraint.hasPoleTarget = true;
            }
            instance.constraints.push_back(std::move(constraint));
        }
        instance.rootMotionTranslation = record.extractedRootMotionTranslation;
        instance.rootMotionRotation = record.extractedRootMotionRotation;
        instance.hasRootMotion = record.hasExtractedRootMotion;
        std::vector<Vec3>& rootMotionTrail = state.animatorDebugRootMotionTrails[id];
        if (rootMotionTrail.empty()) rootMotionTrail.reserve(128U);
        if (record.hasExtractedRootMotion) {
            const Vec3 previous = rootMotionTrail.empty() ? Vec3{} : rootMotionTrail.back();
            rootMotionTrail.push_back(previous + record.extractedRootMotionTranslation);
            if (rootMotionTrail.size() > 128U) rootMotionTrail.erase(rootMotionTrail.begin());
        }
        instance.rootMotionTrail = rootMotionTrail;
        if (record.skeleton.has_value()) {
            const AnimatorInstanceSkeleton& skeleton = *record.skeleton;
            const AnimatorInstanceSkeleton::PoseBuffer& pose = skeleton.poses[skeleton.currentPose];
            instance.skeletonAssetId = skeleton.asset.Id().value;
            instance.skeletonCompatibilitySignature = skeleton.compatibilitySignature;
            instance.poseEvaluationCount = skeleton.evaluationCount;
            instance.hierarchySolveCount = skeleton.hierarchySolveCount;
            instance.paletteMatrixCount = static_cast<std::uint32_t>(pose.skinMatrices.size());
            instance.bones.reserve(skeleton.boneIds.size());
            for (std::size_t index = 0U; index < skeleton.boneIds.size(); ++index) {
                instance.bones.push_back({
                    .id = skeleton.boneIds[index],
                    .localPose = { pose.local.positions[index], pose.local.rotations[index], pose.local.scales[index] },
                    .componentPose = { pose.component.positions[index], pose.component.rotations[index], pose.component.scales[index] },
                });
            }
        }
        if (const DrawD3DeformedGeometryComponent* geometry = scene.Components().DeformedGeometries().TryGet(record.entity);
            geometry != nullptr) {
            instance.skeletalMeshAssetId = geometry->skeletalMeshAssetId;
            instance.lodBias = geometry->lodBias;
            instance.lodEnabled = geometry->lodEnabled;
            instance.fixedBounds = geometry->fixedBounds;
            instance.deformedGeometryEnabled = geometry->enabled;
        }
        published.instances.push_back(std::move(instance));
    }
    state.animatorDebugSnapshots.Publish(std::move(published));
}

void SolveComponentPose(
    const SkeletonAsset& skeleton,
    AnimatorInstanceSkeleton::PoseBuffer& pose) {
    const std::size_t boneCount = skeleton.bones.size();
    for (std::size_t index = 0U; index < boneCount; ++index) {
        const SkeletonBone& bone = skeleton.bones[index];

        if (bone.parentIndex < 0) {
            pose.component.positions[index] = pose.local.positions[index];
            pose.component.rotations[index] = pose.local.rotations[index];
            pose.component.scales[index] = pose.local.scales[index];
        } else {
            const std::size_t parentIndex =
                static_cast<std::size_t>(bone.parentIndex);
            const Vec3 parentScale = pose.component.scales[parentIndex];
            const Vec3 scaledLocalPosition{
                parentScale.x * pose.local.positions[index].x,
                parentScale.y * pose.local.positions[index].y,
                parentScale.z * pose.local.positions[index].z,
            };
            pose.component.positions[index] =
                pose.component.positions[parentIndex] + kb::math::Rotate(
                    pose.component.rotations[parentIndex],
                    scaledLocalPosition);
            pose.component.rotations[index] = kb::math::Normalize(
                pose.component.rotations[parentIndex] *
                pose.local.rotations[index]);
            pose.component.scales[index] = {
                parentScale.x * pose.local.scales[index].x,
                parentScale.y * pose.local.scales[index].y,
                parentScale.z * pose.local.scales[index].z,
            };
        }
        pose.skinMatrices[index] = kb::math::FromTRS(
            pose.component.positions[index],
            pose.component.rotations[index],
            pose.component.scales[index]) * bone.inverseBind;
    }
}

[[nodiscard]] bool IsFiniteTransform(const LocalTransform& transform) noexcept {
    const float rotationLengthSquared =
        transform.rotation.x * transform.rotation.x +
        transform.rotation.y * transform.rotation.y +
        transform.rotation.z * transform.rotation.z +
        transform.rotation.w * transform.rotation.w;
    return std::isfinite(transform.position.x) &&
        std::isfinite(transform.position.y) &&
        std::isfinite(transform.position.z) &&
        std::isfinite(transform.rotation.x) &&
        std::isfinite(transform.rotation.y) &&
        std::isfinite(transform.rotation.z) &&
        std::isfinite(transform.rotation.w) &&
        rotationLengthSquared > 1.0e-8F &&
        std::isfinite(transform.scale.x) &&
        std::isfinite(transform.scale.y) &&
        std::isfinite(transform.scale.z);
}

[[nodiscard]] LocalTransform ComposeTransform(
    const LocalTransform& parent, const LocalTransform& child) noexcept {
    const Vec3 scaledPosition{
        parent.scale.x * child.position.x,
        parent.scale.y * child.position.y,
        parent.scale.z * child.position.z,
    };
    return {
        .position = parent.position + kb::math::Rotate(parent.rotation, scaledPosition),
        .rotation = kb::math::Normalize(parent.rotation * child.rotation),
        .scale = {
            parent.scale.x * child.scale.x,
            parent.scale.y * child.scale.y,
            parent.scale.z * child.scale.z,
        },
    };
}

[[nodiscard]] WorldTransform ComposeWorldTransform(
    const WorldTransform& parent, const LocalTransform& child) noexcept {
    const LocalTransform composed = ComposeTransform(
        { .position = parent.position, .rotation = parent.rotation, .scale = parent.scale }, child);
    return { .position = composed.position, .rotation = composed.rotation, .scale = composed.scale };
}

void ApplySkeletalRigConstraints(
    Scene& scene, AnimatorRuntimeRecord& record,
    AnimatorInstanceSkeleton::PoseBuffer& pose,
    std::optional<std::uint32_t> extractedRootMotionBone);

void InitializePoseBuffers(AnimatorInstanceSkeleton& derived) {
    AnimatorInstanceSkeleton::PoseBuffer& reference = derived.poses[0U];
    const std::size_t boneCount = derived.asset->bones.size();
    reference.local.positions.reserve(boneCount);
    reference.local.rotations.reserve(boneCount);
    reference.local.scales.reserve(boneCount);
    reference.component.positions.resize(boneCount);
    reference.component.rotations.resize(boneCount);
    reference.component.scales.resize(boneCount);
    reference.skinMatrices.resize(boneCount);
    for (const SkeletonBone& bone : derived.asset->bones) {
        reference.local.positions.push_back(bone.referencePose.position);
        reference.local.rotations.push_back(bone.referencePose.rotation);
        reference.local.scales.push_back(bone.referencePose.scale);
    }
    SolveComponentPose(*derived.asset, reference);
    derived.poses[1U] = reference;
    for (AnimatorInstanceSkeleton::PoseSoa& scratch :
         derived.stateScratch) {
        scratch.positions.resize(boneCount);
        scratch.rotations.resize(boneCount);
        scratch.scales.resize(boneCount);
    }
    for (std::vector<std::uint8_t>& touched : derived.stateTouched) {
        touched.resize(boneCount);
    }
    for (std::vector<std::uint8_t>& touched : derived.motionTouched) {
        touched.resize(boneCount);
    }
    derived.currentPose = 0U;
}

[[nodiscard]] bool BindSkeletalMotion(
    Scene& scene, AnimatorRuntimeRecord& instance,
    AnimatorRuntimeState::Motion& motion) {
    const AnimationClip& clip = *motion.clip;
    const SkeletonBindingComponent* authoredBinding =
        scene.Components().SkeletonBindings().TryGet(instance.entity);
    if (authoredBinding == nullptr || !authoredBinding->enabled ||
        !IsSkeletonBindingComponentValid(*authoredBinding) ||
        authoredBinding->skeletonAssetId != clip.targetSkeletonAssetId ||
        authoredBinding->skeletonCompatibilitySignature !=
            clip.targetSkeletonCompatibilitySignature) {
        return false;
    }

    if (!instance.skeleton.has_value()) {
        if (!instance.bindings.empty()) return false;
        auto skeleton = scene.Assets().Manager().Load<SkeletonAsset>(
            kb::assets::AssetId{ authoredBinding->skeletonAssetId });
        if (!skeleton.IsLoaded() ||
            SkeletonCompatibilitySignature(*skeleton) !=
                authoredBinding->skeletonCompatibilitySignature ||
            skeleton->bones.size() >
                static_cast<std::size_t>(
                    std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }

        AnimatorInstanceSkeleton derived{};
        derived.asset = std::move(skeleton);
        derived.loadGeneration = scene.Assets().Manager().LoadGeneration(
            derived.asset.Id());
        derived.compatibilitySignature =
            authoredBinding->skeletonCompatibilitySignature;
        derived.boneIds.reserve(derived.asset->bones.size());
        derived.boneIndices.reserve(derived.asset->bones.size());
        for (std::size_t index = 0U; index < derived.asset->bones.size();
             ++index) {
            const SkeletonBoneId boneId = derived.asset->bones[index].id;
            derived.boneIds.push_back(boneId);
            if (!derived.boneIndices.emplace(
                    boneId, static_cast<std::uint32_t>(index)).second) {
                return false;
            }
        }
        InitializePoseBuffers(derived);
        instance.skeleton.emplace(std::move(derived));
    } else if (instance.skeleton->asset.Id().value !=
                   clip.targetSkeletonAssetId ||
               instance.skeleton->compatibilitySignature !=
                   clip.targetSkeletonCompatibilitySignature) {
        return false;
    }

    motion.boneIndices.reserve(clip.skeletalTracks.size());
    for (const AnimationBoneTrack& track : clip.skeletalTracks) {
        const auto found = instance.skeleton->boneIndices.find(track.boneId);
        if (found == instance.skeleton->boneIndices.end()) return false;
        motion.boneIndices.push_back(found->second);
    }
    if (clip.rootMotionMode == AnimationRootMotionMode::ExtractFromBone) {
        const auto found =
            instance.skeleton->boneIndices.find(clip.rootMotionBoneId);
        if (found == instance.skeleton->boneIndices.end()) return false;
        motion.rootMotionBoneIndex = found->second;
    }
    return true;
}

[[nodiscard]] bool BindMorphTargets(AnimatorRuntimeRecord& instance) {
    if (!instance.skeleton.has_value()) return true;
    AnimatorInstanceSkeleton& skeleton = *instance.skeleton;
    std::vector<std::string> names;
    for (const AnimatorRuntimeLayer& layer : instance.layers) {
        for (const AnimatorRuntimeState& state : layer.states) {
            for (const AnimatorRuntimeState::Motion& motion : state.motions) {
                for (const AnimationMorphTrack& track : motion.clip->morphTracks) {
                    names.push_back(track.morphTarget);
                }
            }
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    skeleton.morphTargetNames = std::move(names);
    for (std::vector<float>& weights : skeleton.morphWeights) {
        weights.assign(skeleton.morphTargetNames.size(), 0.0F);
    }
    for (std::vector<float>& weights : skeleton.stateMorphScratch) {
        weights.assign(skeleton.morphTargetNames.size(), 0.0F);
    }
    for (std::vector<std::uint8_t>& touched : skeleton.stateMorphTouched) {
        touched.assign(skeleton.morphTargetNames.size(), 0U);
    }
    for (AnimatorRuntimeLayer& layer : instance.layers) {
        for (AnimatorRuntimeState& state : layer.states) {
            for (AnimatorRuntimeState::Motion& motion : state.motions) {
                motion.morphIndices.clear();
                motion.morphIndices.reserve(motion.clip->morphTracks.size());
                for (const AnimationMorphTrack& track : motion.clip->morphTracks) {
                    const auto index = std::lower_bound(
                        skeleton.morphTargetNames.begin(), skeleton.morphTargetNames.end(), track.morphTarget);
                    if (index == skeleton.morphTargetNames.end() || *index != track.morphTarget) return false;
                    motion.morphIndices.push_back(static_cast<std::uint32_t>(index - skeleton.morphTargetNames.begin()));
                }
            }
        }
    }
    return true;
}

[[nodiscard]] bool RuntimeSkeletalBindingIsCurrent(
    Scene& scene, const AnimatorRuntimeRecord& instance) {
    if (!instance.skeleton.has_value()) return true;
    const SkeletonBindingComponent* binding =
        scene.Components().SkeletonBindings().TryGet(instance.entity);
    return binding != nullptr && binding->enabled &&
        IsSkeletonBindingComponentValid(*binding) &&
        binding->skeletonAssetId == instance.skeleton->asset.Id().value &&
        binding->skeletonCompatibilitySignature ==
            instance.skeleton->compatibilitySignature &&
        instance.skeleton->loadGeneration ==
            scene.Assets().Manager().LoadGeneration(
                instance.skeleton->asset.Id());
}

void RestoreCompatibleRuntimeState(
    const AnimatorRuntimeRecord& previous,
    AnimatorRuntimeRecord& reloaded) {
    reloaded.speed = previous.speed;
    reloaded.lastAppliedComponentSpeed = previous.lastAppliedComponentSpeed;
    reloaded.ikTargets = previous.ikTargets;

    for (std::size_t reloadedIndex = 0U;
         reloadedIndex < reloaded.controller->parameters.size();
         ++reloadedIndex) {
        const AnimatorParameterDefinition& target =
            reloaded.controller->parameters[reloadedIndex];
        for (std::size_t previousIndex = 0U;
             previousIndex < previous.controller->parameters.size();
             ++previousIndex) {
            const AnimatorParameterDefinition& source =
                previous.controller->parameters[previousIndex];
            if (source.name == target.name && source.type == target.type) {
                reloaded.parameters[reloadedIndex] =
                    previous.parameters[previousIndex];
                break;
            }
        }
    }

    const auto transferTime = [](double time,
                                 const AnimatorRuntimeState& source,
                                 const AnimatorRuntimeState& target) {
        const float sourceDuration = ReferenceClip(source).durationSeconds;
        const float targetDuration = ReferenceClip(target).durationSeconds;
        if (!std::isfinite(time) || sourceDuration <= 0.0F ||
            targetDuration <= 0.0F) {
            return 0.0;
        }
        const float normalized = std::clamp(
            static_cast<float>(time / static_cast<double>(sourceDuration)),
            0.0F, 1.0F);
        return StateTime(normalized, ReferenceClip(target));
    };

    for (std::size_t reloadedLayerIndex = 0U;
         reloadedLayerIndex < reloaded.controller->layers.size();
         ++reloadedLayerIndex) {
        const AnimatorControllerLayer& targetDefinition =
            reloaded.controller->layers[reloadedLayerIndex];
        const AnimatorRuntimeLayer* sourceLayer = nullptr;
        for (std::size_t previousLayerIndex = 0U;
             previousLayerIndex < previous.controller->layers.size();
             ++previousLayerIndex) {
            if (previous.controller->layers[previousLayerIndex].name ==
                targetDefinition.name) {
                sourceLayer = &previous.layers[previousLayerIndex];
                break;
            }
        }
        if (sourceLayer == nullptr) continue;

        const AnimatorControllerLayer& sourceDefinition =
            previous.controller->layers[static_cast<std::size_t>(
                sourceLayer - previous.layers.data())];
        const std::string& sourceStateName =
            sourceDefinition.states[sourceLayer->currentState].name;
        AnimatorRuntimeLayer& targetLayer =
            reloaded.layers[reloadedLayerIndex];
        for (std::size_t targetStateIndex = 0U;
             targetStateIndex < targetDefinition.states.size();
             ++targetStateIndex) {
            if (targetDefinition.states[targetStateIndex].name !=
                sourceStateName) {
                continue;
            }
            targetLayer.currentState = targetStateIndex;
            targetLayer.currentTimeSeconds = transferTime(
                sourceLayer->currentTimeSeconds,
                sourceLayer->states[sourceLayer->currentState],
                targetLayer.states[targetStateIndex]);
            targetLayer.previousState = targetStateIndex;
            targetLayer.previousTimeSeconds = targetLayer.currentTimeSeconds;
            targetLayer.transitioning = false;
            targetLayer.transitionElapsedSeconds = 0.0;
            targetLayer.transitionDurationSeconds = 0.0;
            break;
        }
    }
}

[[nodiscard]] bool RuntimeBindingsMatchCanonicalHierarchy(
    Scene& scene, const AnimatorRuntimeRecord& record) {
    if (record.layers.size() != record.controller->layers.size()) return false;
    for (std::size_t layerIndex = 0U;
         layerIndex < record.layers.size();
         ++layerIndex) {
        const AnimatorRuntimeLayer& runtimeLayer = record.layers[layerIndex];
        const AnimatorControllerLayer& definitionLayer =
            record.controller->layers[layerIndex];
        if (runtimeLayer.states.size() != definitionLayer.states.size()) {
            return false;
        }
        for (std::size_t stateIndex = 0U;
             stateIndex < runtimeLayer.states.size();
             ++stateIndex) {
            const AnimatorRuntimeState& runtimeState =
                runtimeLayer.states[stateIndex];
            for (const AnimatorRuntimeState::Motion& motion :
                 runtimeState.motions) {
                if (motion.targetIndices.size() !=
                    motion.clip->tracks.size()) {
                    return false;
                }
                for (std::size_t trackIndex = 0U;
                     trackIndex < motion.clip->tracks.size();
                     ++trackIndex) {
                    const std::size_t bindingIndex =
                        motion.targetIndices[trackIndex];
                    if (bindingIndex >= record.bindings.size() ||
                        ResolveTarget(
                            scene, record.entity,
                            motion.clip->tracks[trackIndex].targetPath) !=
                            record.bindings[bindingIndex].target) {
                        return false;
                    }
                }
            }
        }
    }
    if (record.rigConstraints.size() !=
        record.controller->rigConstraints.size()) {
        return false;
    }
    for (const AnimatorRuntimeConstraint& constraint :
         record.rigConstraints) {
        if (constraint.definitionIndex >=
            record.controller->rigConstraints.size()) {
            return false;
        }
        const AnimatorRigConstraint& definition =
            record.controller->rigConstraints[constraint.definitionIndex];
        if (record.skeleton.has_value()) {
            const AnimatorInstanceSkeleton& skeleton = *record.skeleton;
            const auto constrained = skeleton.boneIndices.find(
                definition.constrainedBoneId);
            if (constrained == skeleton.boneIndices.end() ||
                constrained->second != constraint.constrainedBoneIndex) {
                return false;
            }
            if (definition.type == AnimatorRigConstraintType::TwoBoneIK) {
                const auto mid = skeleton.boneIndices.find(
                    definition.midBoneId);
                const auto tip = skeleton.boneIndices.find(
                    definition.tipBoneId);
                if (mid == skeleton.boneIndices.end() ||
                    tip == skeleton.boneIndices.end() ||
                    mid->second != constraint.midBoneIndex ||
                    tip->second != constraint.tipBoneIndex ||
                    skeleton.asset->bones[mid->second].parentIndex !=
                        static_cast<std::int32_t>(constrained->second) ||
                    skeleton.asset->bones[tip->second].parentIndex !=
                        static_cast<std::int32_t>(mid->second)) {
                    return false;
                }
            }
            continue;
        }
        if (ResolveTarget(
                scene, record.entity, definition.constrainedPath) !=
            constraint.constrained) {
            return false;
        }
        if (definition.type == AnimatorRigConstraintType::TwoBoneIK &&
            (ResolveTarget(scene, record.entity, definition.midPath) !=
                 constraint.mid ||
             ResolveTarget(scene, record.entity, definition.tipPath) !=
                 constraint.tip)) {
            return false;
        }
    }
    return true;
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

LocalTransform Sample(const AnimationBoneTrack& track, float time) {
    if (time <= track.keyframes.front().timeSeconds) {
        return track.keyframes.front().transform;
    }
    if (time >= track.keyframes.back().timeSeconds) {
        return track.keyframes.back().transform;
    }
    const auto upper = std::upper_bound(
        track.keyframes.begin(), track.keyframes.end(), time,
        [](float value, const AnimationBoneKeyframe& key) {
            return value < key.timeSeconds;
        });
    const AnimationBoneKeyframe& b = *upper;
    const AnimationBoneKeyframe& a = *(upper - 1);
    const float alpha =
        (time - a.timeSeconds) / (b.timeSeconds - a.timeSeconds);
    return LocalTransform{
        .position = {
            kb::math::Lerp(
                a.transform.position.x, b.transform.position.x, alpha),
            kb::math::Lerp(
                a.transform.position.y, b.transform.position.y, alpha),
            kb::math::Lerp(
                a.transform.position.z, b.transform.position.z, alpha),
        },
        .rotation = kb::math::Slerp(
            a.transform.rotation, b.transform.rotation, alpha),
        .scale = {
            kb::math::Lerp(
                a.transform.scale.x, b.transform.scale.x, alpha),
            kb::math::Lerp(
                a.transform.scale.y, b.transform.scale.y, alpha),
            kb::math::Lerp(
                a.transform.scale.z, b.transform.scale.z, alpha),
        },
    };
}

[[nodiscard]] float Sample(const AnimationMorphTrack& track, float time) {
    if (time <= track.keyframes.front().timeSeconds) return track.keyframes.front().weight;
    if (time >= track.keyframes.back().timeSeconds) return track.keyframes.back().weight;
    const auto upper = std::upper_bound(
        track.keyframes.begin(), track.keyframes.end(), time,
        [](float value, const AnimationMorphKeyframe& key) { return value < key.timeSeconds; });
    const AnimationMorphKeyframe& after = *upper;
    const AnimationMorphKeyframe& before = *(upper - 1);
    const float alpha = (time - before.timeSeconds) / (after.timeSeconds - before.timeSeconds);
    return kb::math::Lerp(before.weight, after.weight, alpha);
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

[[nodiscard]] RootMotionDelta RootPose(
    const AnimationBoneTrack& track, float timeSeconds) {
    const LocalTransform pose = Sample(track, timeSeconds);
    return RootMotionDelta{
        .translation = pose.position,
        .rotation = pose.rotation,
    };
}

[[nodiscard]] const AnimationClip& ReferenceClip(
    const AnimatorRuntimeState& state) {
    return *state.motions.front().clip;
}

struct BlendSelection {
    std::size_t lower = 0U;
    std::size_t upper = 0U;
    float alpha = 0.0F;
};

[[nodiscard]] BlendSelection SelectBlend(
    const AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& definition) {
    if (state.motions.size() == 1U) return {};
    const float value = record.parameters[state.blendParameterIndex].floatValue;
    const auto upper = std::upper_bound(
        definition.blendChildren.begin(), definition.blendChildren.end(), value,
        [](float needle, const AnimatorControllerState::BlendChild& child) {
            return needle < child.threshold;
        });
    if (upper == definition.blendChildren.begin()) return {};
    if (upper == definition.blendChildren.end()) {
        const std::size_t last = definition.blendChildren.size() - 1U;
        return BlendSelection{ .lower = last, .upper = last };
    }
    const std::size_t upperIndex =
        static_cast<std::size_t>(upper - definition.blendChildren.begin());
    const std::size_t lowerIndex = upperIndex - 1U;
    const float range =
        definition.blendChildren[upperIndex].threshold -
        definition.blendChildren[lowerIndex].threshold;
    return BlendSelection{
        .lower = lowerIndex,
        .upper = upperIndex,
        .alpha = std::clamp(
            (value - definition.blendChildren[lowerIndex].threshold) / range,
            0.0F, 1.0F),
    };
}

[[nodiscard]] const AnimationTransformTrack* RootTrack(
    const AnimatorRuntimeState::Motion& motion,
    const AnimatorControllerLayer& layer) {
    const AnimationClip& clip = *motion.clip;
    for (const AnimationTransformTrack& track : clip.tracks) {
        if (track.targetPath.empty() && (track.bindingMask & layer.mask) != 0U) return &track;
    }
    return nullptr;
}

[[nodiscard]] const AnimationBoneTrack* SkeletalRootTrack(
    const AnimatorRuntimeState::Motion& motion,
    const AnimatorControllerLayer& layer) {
    if (motion.clip->rootMotionMode !=
            AnimationRootMotionMode::ExtractFromBone ||
        motion.rootMotionBoneIndex ==
            std::numeric_limits<std::uint32_t>::max()) {
        return nullptr;
    }
    for (std::size_t trackIndex = 0U;
         trackIndex < motion.clip->skeletalTracks.size(); ++trackIndex) {
        const AnimationBoneTrack& track =
            motion.clip->skeletalTracks[trackIndex];
        if (motion.boneIndices[trackIndex] == motion.rootMotionBoneIndex &&
            (track.bindingMask & layer.mask) != 0U) {
            return &track;
        }
    }
    return nullptr;
}

[[nodiscard]] RootMotionDelta MotionRootMotion(
    const AnimatorRuntimeState::Motion& motion,
    const AnimatorControllerLayer& layer,
    double startSeconds,
    double deltaSeconds) {
    const AnimationClip& clip = *motion.clip;
    const AnimationTransformTrack* transformTrack =
        RootTrack(motion, layer);
    const AnimationBoneTrack* boneTrack =
        SkeletalRootTrack(motion, layer);
    if (transformTrack == nullptr && boneTrack == nullptr) {
        throw std::runtime_error(
            "Root motion requires a bound source track on the controller's first layer");
    }
    const auto sampleRoot = [&](float timeSeconds) {
        return boneTrack != nullptr
            ? RootPose(*boneTrack, timeSeconds)
            : RootPose(*transformTrack, timeSeconds);
    };
    const RootMotionDelta start =
        sampleRoot(static_cast<float>(startSeconds));
    if (!clip.looping) {
        const float end = static_cast<float>(std::min(
            startSeconds + deltaSeconds, static_cast<double>(clip.durationSeconds)));
        return Relative(start, sampleRoot(end));
    }

    const double duration = static_cast<double>(clip.durationSeconds);
    const double unwrappedEnd = startSeconds + deltaSeconds;
    const std::size_t cycles = static_cast<std::size_t>(std::floor(unwrappedEnd / duration));
    double wrappedEnd = std::fmod(unwrappedEnd, duration);
    if (wrappedEnd < 0.0) wrappedEnd += duration;
    const RootMotionDelta first = sampleRoot(0.0F);
    const RootMotionDelta cycle =
        Relative(first, sampleRoot(clip.durationSeconds));
    const RootMotionDelta endFromFirst =
        Relative(first, sampleRoot(static_cast<float>(wrappedEnd)));
    return Relative(Relative(first, start), Compose(Pow(cycle, cycles), endFromFirst));
}

[[nodiscard]] RootMotionDelta StateRootMotion(
    const AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& stateDefinition,
    const AnimatorControllerLayer& layer,
    double startSeconds,
    double deltaSeconds) {
    const BlendSelection selection = SelectBlend(record, state, stateDefinition);
    RootMotionDelta lower = MotionRootMotion(
        state.motions[selection.lower], layer, startSeconds, deltaSeconds);
    if (selection.lower == selection.upper) return lower;
    const RootMotionDelta upper = MotionRootMotion(
        state.motions[selection.upper], layer, startSeconds, deltaSeconds);
    lower.translation = lower.translation +
        (upper.translation - lower.translation) * selection.alpha;
    lower.rotation = kb::math::Slerp(lower.rotation, upper.rotation, selection.alpha);
    return lower;
}

[[nodiscard]] RootMotionDelta ExtractRootMotion(
    const AnimatorRuntimeRecord& record,
    double scaledDelta,
    double frameDelta) {
    if (scaledDelta <= 0.0 || record.layers.empty()) return {};
    const AnimatorControllerLayer& definition = record.controller->layers.front();
    const AnimatorRuntimeLayer& layer = record.layers.front();
    RootMotionDelta delta = StateRootMotion(
        record, layer.states[layer.currentState],
        definition.states[layer.currentState], definition,
        layer.currentTimeSeconds, scaledDelta);
    if (layer.transitioning) {
        const RootMotionDelta previous = StateRootMotion(
            record, layer.states[layer.previousState],
            definition.states[layer.previousState], definition,
            layer.previousTimeSeconds, scaledDelta);
        const double transitionStart = std::clamp(
            layer.transitionElapsedSeconds / layer.transitionDurationSeconds, 0.0, 1.0);
        const double transitionEnd = std::clamp(
            (layer.transitionElapsedSeconds + frameDelta) / layer.transitionDurationSeconds, 0.0, 1.0);
        const double transitioningDuration = std::clamp(
            layer.transitionDurationSeconds - layer.transitionElapsedSeconds, 0.0, frameDelta);
        const double transitionRemainder = frameDelta - transitioningDuration;
        const float transition = frameDelta > 0.0
            ? static_cast<float>(
                  (transitioningDuration * (transitionStart + transitionEnd) * 0.5 +
                   transitionRemainder) /
                  frameDelta)
            : static_cast<float>(transitionStart);
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

[[nodiscard]] const AnimatorRuntimeState::Motion& EventMotion(
    const AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& definition) {
    // A blend tree emits one deterministic marker stream: the dominant
    // child (ties select the upper child). This avoids duplicate gameplay
    // events while both clips contribute to the pose.
    const BlendSelection selection = SelectBlend(record, state, definition);
    return state.motions[
        selection.alpha < 0.5F ? selection.lower : selection.upper];
}

std::size_t EventCrossingCount(
    const AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& definition,
    double start,
    double delta,
    std::size_t limit) {
    const AnimatorRuntimeState::Motion& motion =
        EventMotion(record, state, definition);
    if (delta <= 0.0 || motion.clip->events.empty()) return 0U;
    const double duration = static_cast<double>(motion.clip->durationSeconds);
    std::size_t total = 0U;
    for (const AnimationEventKeyframe& event : motion.clip->events) {
        std::size_t count = 0U;
        if (motion.clip->looping) {
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
    const AnimatorRuntimeRecord& record,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& definition,
    double start,
    double delta,
    SceneEntity target,
    std::string_view layerName,
    std::string_view stateName,
    std::vector<AnimationEventRecord>& pending) {
    const AnimatorRuntimeState::Motion& motion =
        EventMotion(record, state, definition);
    if (delta <= 0.0 || motion.clip->events.empty()) return;
    const double duration = static_cast<double>(motion.clip->durationSeconds);
    const auto append = [&](const AnimationEventKeyframe& event) {
        pending.push_back(AnimationEventRecord{
            .target = target,
            .eventId = event.id,
            .clipAssetId = motion.clip.Id().value,
            .layer = std::string{ layerName },
            .state = std::string{ stateName },
            .normalizedTime = event.timeSeconds / motion.clip->durationSeconds,
        });
    };
    if (!motion.clip->looping) {
        const double end = std::min(start + delta, duration);
        for (const AnimationEventKeyframe& event : motion.clip->events) {
            if (static_cast<double>(event.timeSeconds) > start && static_cast<double>(event.timeSeconds) <= end) append(event);
        }
        return;
    }

    const double end = start + delta;
    const std::size_t lastCycle = static_cast<std::size_t>(std::floor(end / duration));
    for (std::size_t cycle = 0U; cycle <= lastCycle; ++cycle) {
        const double cycleOffset = static_cast<double>(cycle) * duration;
        for (const AnimationEventKeyframe& event : motion.clip->events) {
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
    const AnimatorControllerState& definition,
    double time,
    std::uint64_t mask,
    bool from) {
    const BlendSelection selection = SelectBlend(record, state, definition);
    const float sampleTime = static_cast<float>(time);
    for (std::size_t bindingIndex = 0U;
         bindingIndex < record.bindings.size(); ++bindingIndex) {
        const auto sampleMotion = [&](std::size_t motionIndex)
            -> std::optional<LocalTransform> {
            const AnimatorRuntimeState::Motion& motion = state.motions[motionIndex];
            const AnimationClip& clip = *motion.clip;
            for (std::size_t trackIndex = 0U;
                 trackIndex < clip.tracks.size(); ++trackIndex) {
                if (motion.targetIndices[trackIndex] != bindingIndex ||
                    (clip.tracks[trackIndex].bindingMask & mask) == 0U) continue;
                return Sample(clip.tracks[trackIndex], sampleTime);
            }
            return std::nullopt;
        };
        const std::optional<LocalTransform> lower = sampleMotion(selection.lower);
        const std::optional<LocalTransform> upper =
            selection.upper == selection.lower
                ? lower
                : sampleMotion(selection.upper);
        if (!lower && !upper) continue;
        const LocalTransform pose = selection.lower == selection.upper
            ? lower.value_or(record.bindings[bindingIndex].bindTransform)
            : Blend(
                  lower.value_or(record.bindings[bindingIndex].bindTransform),
                  upper.value_or(record.bindings[bindingIndex].bindTransform),
                  selection.alpha);
        AnimatorRuntimeBinding& binding = record.bindings[bindingIndex];
        if (from) {
            binding.fromPose = pose;
            binding.fromTouched = true;
        } else {
            binding.toPose = pose;
            binding.toTouched = true;
        }
    }
}

[[nodiscard]] LocalTransform ReadPose(
    const AnimatorInstanceSkeleton::PoseSoa& pose,
    std::size_t boneIndex) {
    return LocalTransform{
        .position = pose.positions[boneIndex],
        .rotation = pose.rotations[boneIndex],
        .scale = pose.scales[boneIndex],
    };
}

void WritePose(
    AnimatorInstanceSkeleton::PoseSoa& pose, std::size_t boneIndex,
    const LocalTransform& value) {
    pose.positions[boneIndex] = value.position;
    pose.rotations[boneIndex] = value.rotation;
    pose.scales[boneIndex] = value.scale;
}

void EvaluateIndexedSkeletalState(
    AnimatorRuntimeRecord& instance,
    const AnimatorRuntimeState& state,
    const AnimatorControllerState& definition,
    double time, std::uint64_t mask, std::size_t scratchIndex) {
    AnimatorInstanceSkeleton& skeleton = *instance.skeleton;
    AnimatorInstanceSkeleton::PoseSoa& output =
        skeleton.stateScratch[scratchIndex];
    std::vector<std::uint8_t>& outputTouched =
        skeleton.stateTouched[scratchIndex];
    std::vector<std::uint8_t>& lowerTouched =
        skeleton.motionTouched[0U];
    std::vector<std::uint8_t>& upperTouched =
        skeleton.motionTouched[1U];
    std::vector<float>& outputMorphWeights =
        skeleton.stateMorphScratch[scratchIndex];
    std::vector<std::uint8_t>& outputMorphTouched =
        skeleton.stateMorphTouched[scratchIndex];
    std::fill(outputTouched.begin(), outputTouched.end(), std::uint8_t{ 0U });
    std::fill(lowerTouched.begin(), lowerTouched.end(), std::uint8_t{ 0U });
    std::fill(upperTouched.begin(), upperTouched.end(), std::uint8_t{ 0U });
    std::fill(outputMorphWeights.begin(), outputMorphWeights.end(), 0.0F);
    std::fill(outputMorphTouched.begin(), outputMorphTouched.end(), std::uint8_t{ 0U });

    const BlendSelection selection = SelectBlend(instance, state, definition);
    const float sampleTime = static_cast<float>(time);
    const auto sampleMotion = [&](std::size_t motionIndex,
                                  std::vector<std::uint8_t>& touched,
                                  bool blendUpper) {
        const AnimatorRuntimeState::Motion& motion =
            state.motions[motionIndex];
        if (motion.boneIndices.size() !=
            motion.clip->skeletalTracks.size()) {
            throw std::runtime_error(
                "AnimatorInstance lost its prebound skeletal track indices");
        }
        for (std::size_t trackIndex = 0U;
             trackIndex < motion.clip->skeletalTracks.size(); ++trackIndex) {
            const AnimationBoneTrack& track =
                motion.clip->skeletalTracks[trackIndex];
            if ((track.bindingMask & mask) == 0U) continue;
            const std::uint32_t boneIndex =
                motion.boneIndices[trackIndex];
            if (boneIndex >= output.positions.size()) {
                throw std::runtime_error(
                    "AnimatorInstance contains an out-of-range bone index");
            }
            LocalTransform sampled = Sample(track, sampleTime);
            if (blendUpper) {
                const LocalTransform lower = lowerTouched[boneIndex] != 0U
                    ? ReadPose(output, boneIndex)
                    : skeleton.asset->bones[boneIndex].referencePose;
                sampled = Blend(lower, sampled, selection.alpha);
            }
            WritePose(output, boneIndex, sampled);
            touched[boneIndex] = 1U;
            outputTouched[boneIndex] = 1U;
        }
        if (motion.morphIndices.size() != motion.clip->morphTracks.size()) {
            throw std::runtime_error("AnimatorInstance lost its prebound morph track indices");
        }
        for (std::size_t trackIndex = 0U;
             trackIndex < motion.clip->morphTracks.size(); ++trackIndex) {
            const AnimationMorphTrack& track = motion.clip->morphTracks[trackIndex];
            const std::uint32_t morphIndex = motion.morphIndices[trackIndex];
            if (morphIndex >= outputMorphWeights.size()) {
                throw std::runtime_error("AnimatorInstance contains an out-of-range morph index");
            }
            const float sampled = Sample(track, sampleTime);
            const float lower = outputMorphTouched[morphIndex] != 0U
                ? outputMorphWeights[morphIndex] : 0.0F;
            outputMorphWeights[morphIndex] = blendUpper
                ? kb::math::Lerp(lower, sampled, selection.alpha) : sampled;
            outputMorphTouched[morphIndex] = 1U;
        }
    };

    sampleMotion(selection.lower, lowerTouched, false);
    if (selection.lower == selection.upper) return;
    sampleMotion(selection.upper, upperTouched, true);
    for (std::size_t boneIndex = 0U;
         boneIndex < lowerTouched.size(); ++boneIndex) {
        if (lowerTouched[boneIndex] == 0U ||
            upperTouched[boneIndex] != 0U) {
            continue;
        }
        WritePose(
            output, boneIndex,
            Blend(
                ReadPose(output, boneIndex),
                skeleton.asset->bones[boneIndex].referencePose,
                selection.alpha));
    }
}

void EvaluateIndexedSkeletalPose(
    Scene& scene, AnimatorRuntimeRecord& instance,
    std::optional<std::uint32_t> extractedRootMotionBone) {
    AnimatorInstanceSkeleton& skeleton = *instance.skeleton;
    if (skeleton.evaluationCount ==
            std::numeric_limits<std::uint64_t>::max() ||
        skeleton.hierarchySolveCount ==
            std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "AnimatorInstance pose evaluation counter exhausted");
    }
    ++skeleton.evaluationCount;
    skeleton.currentPose ^= 1U;
    AnimatorInstanceSkeleton::PoseBuffer& current =
        skeleton.poses[skeleton.currentPose];
    std::vector<float>& currentMorphWeights = skeleton.morphWeights[skeleton.currentPose];
    std::fill(currentMorphWeights.begin(), currentMorphWeights.end(), 0.0F);
    for (std::size_t index = 0U; index < skeleton.asset->bones.size();
         ++index) {
        WritePose(
            current.local, index,
            skeleton.asset->bones[index].referencePose);
    }

    for (std::size_t layerIndex = 0U;
         layerIndex < instance.layers.size(); ++layerIndex) {
        const AnimatorRuntimeLayer& layer = instance.layers[layerIndex];
        const AnimatorControllerLayer& layerDefinition =
            instance.controller->layers[layerIndex];
        if (layer.transitioning) {
            EvaluateIndexedSkeletalState(
                instance, layer.states[layer.previousState],
                layerDefinition.states[layer.previousState],
                layer.previousTimeSeconds, layerDefinition.mask, 0U);
            EvaluateIndexedSkeletalState(
                instance, layer.states[layer.currentState],
                layerDefinition.states[layer.currentState],
                layer.currentTimeSeconds, layerDefinition.mask, 1U);
            const float transition = static_cast<float>(std::clamp(
                layer.transitionElapsedSeconds /
                    layer.transitionDurationSeconds,
                0.0, 1.0));
            for (std::size_t boneIndex = 0U;
                 boneIndex < skeleton.asset->bones.size(); ++boneIndex) {
                const bool fromTouched =
                    skeleton.stateTouched[0U][boneIndex] != 0U;
                const bool toTouched =
                    skeleton.stateTouched[1U][boneIndex] != 0U;
                if (!fromTouched && !toTouched) continue;
                const LocalTransform& reference =
                    skeleton.asset->bones[boneIndex].referencePose;
                const LocalTransform from = fromTouched
                    ? ReadPose(skeleton.stateScratch[0U], boneIndex)
                    : reference;
                const LocalTransform to = toTouched
                    ? ReadPose(skeleton.stateScratch[1U], boneIndex)
                    : reference;
                WritePose(
                    current.local, boneIndex,
                    Blend(
                        ReadPose(current.local, boneIndex),
                        Blend(from, to, transition),
                        layerDefinition.weight));
            }
            for (std::size_t morphIndex = 0U;
                 morphIndex < currentMorphWeights.size(); ++morphIndex) {
                const float from = skeleton.stateMorphTouched[0U][morphIndex] != 0U
                    ? skeleton.stateMorphScratch[0U][morphIndex] : 0.0F;
                const float to = skeleton.stateMorphTouched[1U][morphIndex] != 0U
                    ? skeleton.stateMorphScratch[1U][morphIndex] : 0.0F;
                if (skeleton.stateMorphTouched[0U][morphIndex] == 0U &&
                    skeleton.stateMorphTouched[1U][morphIndex] == 0U) continue;
                currentMorphWeights[morphIndex] = kb::math::Lerp(
                    currentMorphWeights[morphIndex], kb::math::Lerp(from, to, transition), layerDefinition.weight);
            }
        } else {
            EvaluateIndexedSkeletalState(
                instance, layer.states[layer.currentState],
                layerDefinition.states[layer.currentState],
                layer.currentTimeSeconds, layerDefinition.mask, 1U);
            for (std::size_t boneIndex = 0U;
                 boneIndex < skeleton.asset->bones.size(); ++boneIndex) {
                if (skeleton.stateTouched[1U][boneIndex] == 0U) continue;
                WritePose(
                    current.local, boneIndex,
                    Blend(
                        ReadPose(current.local, boneIndex),
                        ReadPose(skeleton.stateScratch[1U], boneIndex),
                        layerDefinition.weight));
            }
            for (std::size_t morphIndex = 0U;
                 morphIndex < currentMorphWeights.size(); ++morphIndex) {
                if (skeleton.stateMorphTouched[1U][morphIndex] == 0U) continue;
                currentMorphWeights[morphIndex] = kb::math::Lerp(
                    currentMorphWeights[morphIndex], skeleton.stateMorphScratch[1U][morphIndex], layerDefinition.weight);
            }
        }
    }
    if (extractedRootMotionBone.has_value()) {
        const std::size_t boneIndex = *extractedRootMotionBone;
        if (boneIndex >= skeleton.asset->bones.size()) {
            throw std::runtime_error(
                "AnimatorInstance root-motion bone index is out of range");
        }
        const LocalTransform& reference =
            skeleton.asset->bones[boneIndex].referencePose;
        current.local.positions[boneIndex] = reference.position;
        current.local.rotations[boneIndex] = reference.rotation;
    }
    SolveComponentPose(*skeleton.asset, current);
    ApplySkeletalRigConstraints(
        scene, instance, current, extractedRootMotionBone);
    ++skeleton.hierarchySolveCount;
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

[[nodiscard]] bool IsFinite(const AnimatorIkTarget& target) noexcept {
    return std::isfinite(target.worldPosition.x) &&
        std::isfinite(target.worldPosition.y) &&
        std::isfinite(target.worldPosition.z) &&
        std::isfinite(target.worldRotation.x) &&
        std::isfinite(target.worldRotation.y) &&
        std::isfinite(target.worldRotation.z) &&
        std::isfinite(target.worldRotation.w) &&
        target.worldRotation.x * target.worldRotation.x +
                target.worldRotation.y * target.worldRotation.y +
                target.worldRotation.z * target.worldRotation.z +
                target.worldRotation.w * target.worldRotation.w >
            1.0e-8F &&
        std::isfinite(target.positionWeight) &&
        std::isfinite(target.rotationWeight) &&
        target.positionWeight >= 0.0F && target.positionWeight <= 1.0F &&
        target.rotationWeight >= 0.0F && target.rotationWeight <= 1.0F;
}

[[nodiscard]] float SafeDivide(float numerator, float denominator) noexcept {
    return std::abs(denominator) <= 1.0e-6F ? numerator : numerator / denominator;
}

void SetWorldPose(
    Scene& scene, SceneEntity entity, Vec3 worldPosition, Quat worldRotation,
    bool setPosition) {
    TransformComponent* transform = scene.Transforms().TryGet(entity);
    if (transform == nullptr) {
        throw std::runtime_error("Animator rig constraint target has no Transform");
    }
    const SceneEntity parent = scene.Hierarchy().Parent(entity);
    if (!parent.IsValid()) {
        if (setPosition) transform->localPosition = worldPosition;
        transform->localRotation = kb::math::Normalize(worldRotation);
    } else {
        const TransformComponent* parentTransform =
            scene.Transforms().TryGet(parent);
        if (parentTransform == nullptr) {
            throw std::runtime_error("Animator rig constraint parent has no Transform");
        }
        const Quat inverseParent =
            kb::math::Inverse(parentTransform->worldRotation);
        if (setPosition) {
            const Vec3 unrotated = kb::math::Rotate(
                inverseParent, worldPosition - parentTransform->worldPosition);
            transform->localPosition = Vec3{
                SafeDivide(unrotated.x, parentTransform->worldScale.x),
                SafeDivide(unrotated.y, parentTransform->worldScale.y),
                SafeDivide(unrotated.z, parentTransform->worldScale.z),
            };
        }
        transform->localRotation =
            kb::math::Normalize(inverseParent * worldRotation);
    }
    scene.Transforms().MarkModified(entity);
}

[[nodiscard]] Vec3 StableBendDirection(
    Vec3 direction, Vec3 poleDirection, Vec3 currentBoneDirection) {
    Vec3 normal = kb::math::Cross(direction, poleDirection);
    if (kb::math::Dot(normal, normal) <= 1.0e-8F) {
        normal = kb::math::Cross(direction, currentBoneDirection);
    }
    if (kb::math::Dot(normal, normal) <= 1.0e-8F) {
        const Vec3 fallback =
            std::abs(direction.y) < 0.99F ? Vec3{ 0.0F, 1.0F, 0.0F }
                                         : Vec3{ 0.0F, 0.0F, 1.0F };
        normal = kb::math::Cross(direction, fallback);
    }
    return kb::math::Normalize(
        kb::math::Cross(kb::math::Normalize(normal), direction));
}

void ApplyTwoBoneIk(
    Scene& scene, const AnimatorRuntimeConstraint& constraint,
    const AnimatorRigConstraint& definition,
    const AnimatorIkTarget& target, const AnimatorIkTarget* pole) {
    const TransformComponent* root =
        scene.Transforms().TryGet(constraint.constrained);
    const TransformComponent* mid = scene.Transforms().TryGet(constraint.mid);
    const TransformComponent* tip = scene.Transforms().TryGet(constraint.tip);
    if (root == nullptr || mid == nullptr || tip == nullptr) {
        throw std::runtime_error("TwoBoneIK lost a bound Transform");
    }
    const Vec3 rootPosition = root->worldPosition;
    const Vec3 midPosition = mid->worldPosition;
    const Vec3 tipPosition = tip->worldPosition;
    const float upperLength = kb::math::Length(midPosition - rootPosition);
    const float lowerLength = kb::math::Length(tipPosition - midPosition);
    if (upperLength <= 1.0e-5F || lowerLength <= 1.0e-5F) {
        throw std::runtime_error("TwoBoneIK requires non-zero bone lengths");
    }
    const float positionWeight = definition.weight * target.positionWeight;
    if (positionWeight > 0.0F) {
        Vec3 targetDelta = target.worldPosition - rootPosition;
        float targetDistance = kb::math::Length(targetDelta);
        if (targetDistance <= 1.0e-5F) {
            throw std::runtime_error(
                "TwoBoneIK target cannot coincide with its root");
        }
        const Vec3 targetDirection = targetDelta * (1.0F / targetDistance);
        targetDistance = std::clamp(
            targetDistance,
            std::abs(upperLength - lowerLength) + 1.0e-5F,
            upperLength + lowerLength - 1.0e-5F);
        const Vec3 poleDirection = pole != nullptr
            ? pole->worldPosition - rootPosition
            : midPosition - rootPosition;
        const Vec3 bendDirection = StableBendDirection(
            targetDirection, poleDirection, midPosition - rootPosition);
        const float cosine = std::clamp(
            (upperLength * upperLength + targetDistance * targetDistance -
             lowerLength * lowerLength) /
                (2.0F * upperLength * targetDistance),
            -1.0F, 1.0F);
        const float sine =
            std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
        const Vec3 desiredMid = rootPosition +
            targetDirection * (cosine * upperLength) +
            bendDirection * (sine * upperLength);
        const Quat desiredRootRotation = kb::math::Normalize(
            kb::math::FromToRotation(
                midPosition - rootPosition, desiredMid - rootPosition) *
            root->worldRotation);
        SetWorldPose(
            scene, constraint.constrained, rootPosition,
            kb::math::Slerp(
                root->worldRotation, desiredRootRotation, positionWeight),
            false);
        scene.Runtime().SynchronizeTransforms();

        mid = scene.Transforms().TryGet(constraint.mid);
        tip = scene.Transforms().TryGet(constraint.tip);
        const Quat desiredMidRotation = kb::math::Normalize(
            kb::math::FromToRotation(
                tip->worldPosition - mid->worldPosition,
                target.worldPosition - mid->worldPosition) *
            mid->worldRotation);
        SetWorldPose(
            scene, constraint.mid, mid->worldPosition,
            kb::math::Slerp(
                mid->worldRotation, desiredMidRotation, positionWeight),
            false);
        scene.Runtime().SynchronizeTransforms();
    }

    tip = scene.Transforms().TryGet(constraint.tip);
    const float rotationWeight = definition.weight * target.rotationWeight;
    if (rotationWeight > 0.0F) {
        SetWorldPose(
            scene, constraint.tip, tip->worldPosition,
            kb::math::Slerp(
                tip->worldRotation, target.worldRotation, rotationWeight),
            false);
        scene.Runtime().SynchronizeTransforms();
    }
}

[[nodiscard]] bool IsBoneDescendant(
    const SkeletonAsset& skeleton, std::size_t boneIndex,
    std::size_t ancestorIndex) {
    std::int32_t current = static_cast<std::int32_t>(boneIndex);
    while (current >= 0) {
        if (static_cast<std::size_t>(current) == ancestorIndex) return true;
        current = skeleton.bones[static_cast<std::size_t>(current)].parentIndex;
    }
    return false;
}

void UpdateComponentSubtree(
    const SkeletonAsset& skeleton,
    AnimatorInstanceSkeleton::PoseBuffer& pose,
    std::size_t rootIndex) {
    for (std::size_t index = rootIndex; index < skeleton.bones.size(); ++index) {
        if (!IsBoneDescendant(skeleton, index, rootIndex)) continue;
        const SkeletonBone& bone = skeleton.bones[index];
        if (bone.parentIndex < 0) {
            pose.component.positions[index] = pose.local.positions[index];
            pose.component.rotations[index] = pose.local.rotations[index];
            pose.component.scales[index] = pose.local.scales[index];
        } else {
            const std::size_t parent =
                static_cast<std::size_t>(bone.parentIndex);
            const Vec3 parentScale = pose.component.scales[parent];
            const Vec3 scaled{
                parentScale.x * pose.local.positions[index].x,
                parentScale.y * pose.local.positions[index].y,
                parentScale.z * pose.local.positions[index].z,
            };
            pose.component.positions[index] =
                pose.component.positions[parent] + kb::math::Rotate(
                    pose.component.rotations[parent], scaled);
            pose.component.rotations[index] = kb::math::Normalize(
                pose.component.rotations[parent] * pose.local.rotations[index]);
            pose.component.scales[index] = {
                parentScale.x * pose.local.scales[index].x,
                parentScale.y * pose.local.scales[index].y,
                parentScale.z * pose.local.scales[index].z,
            };
        }
        pose.skinMatrices[index] = kb::math::FromTRS(
            pose.component.positions[index], pose.component.rotations[index],
            pose.component.scales[index]) * bone.inverseBind;
    }
}

void SetBoneComponentPose(
    const SkeletonAsset& skeleton,
    AnimatorInstanceSkeleton::PoseBuffer& pose, std::size_t boneIndex,
    Vec3 componentPosition, Quat componentRotation, bool setPosition) {
    const std::int32_t parentIndex = skeleton.bones[boneIndex].parentIndex;
    if (parentIndex < 0) {
        if (setPosition) pose.local.positions[boneIndex] = componentPosition;
        pose.local.rotations[boneIndex] =
            kb::math::Normalize(componentRotation);
    } else {
        const std::size_t parent = static_cast<std::size_t>(parentIndex);
        const Quat inverseParent =
            kb::math::Inverse(pose.component.rotations[parent]);
        if (setPosition) {
            const Vec3 unrotated = kb::math::Rotate(
                inverseParent,
                componentPosition - pose.component.positions[parent]);
            const Vec3 parentScale = pose.component.scales[parent];
            pose.local.positions[boneIndex] = {
                SafeDivide(unrotated.x, parentScale.x),
                SafeDivide(unrotated.y, parentScale.y),
                SafeDivide(unrotated.z, parentScale.z),
            };
        }
        pose.local.rotations[boneIndex] = kb::math::Normalize(
            inverseParent * componentRotation);
    }
    UpdateComponentSubtree(skeleton, pose, boneIndex);
}

[[nodiscard]] Vec3 WorldToAnimatorPosition(
    const TransformComponent& owner, Vec3 worldPosition) {
    const Vec3 unrotated = kb::math::Rotate(
        kb::math::Inverse(owner.worldRotation),
        worldPosition - owner.worldPosition);
    return {
        SafeDivide(unrotated.x, owner.worldScale.x),
        SafeDivide(unrotated.y, owner.worldScale.y),
        SafeDivide(unrotated.z, owner.worldScale.z),
    };
}

[[nodiscard]] Quat WorldToAnimatorRotation(
    const TransformComponent& owner, Quat worldRotation) {
    return kb::math::Normalize(
        kb::math::Inverse(owner.worldRotation) * worldRotation);
}

void ApplySkeletalRigConstraints(
    Scene& scene, AnimatorRuntimeRecord& record,
    AnimatorInstanceSkeleton::PoseBuffer& pose,
    std::optional<std::uint32_t> extractedRootMotionBone) {
    if (record.rigConstraints.empty()) return;
    const TransformComponent* owner = scene.Transforms().TryGet(record.entity);
    if (owner == nullptr) {
        throw std::runtime_error("Skeletal Animator owner has no Transform");
    }
    const SkeletonAsset& skeleton = *record.skeleton->asset;
    for (const AnimatorRuntimeConstraint& constraint : record.rigConstraints) {
        const AnimatorRigConstraint& definition =
            record.controller->rigConstraints[constraint.definitionIndex];
        if (extractedRootMotionBone.has_value() &&
            (constraint.constrainedBoneIndex == *extractedRootMotionBone ||
             constraint.midBoneIndex == *extractedRootMotionBone ||
             constraint.tipBoneIndex == *extractedRootMotionBone)) {
            throw std::runtime_error(
                "Rig constraint cannot drive the root-motion source bone");
        }
        const auto targetIt = record.ikTargets.find(definition.target);
        if (targetIt == record.ikTargets.end()) continue;
        const AnimatorIkTarget& target = targetIt->second;
        const Vec3 targetPosition =
            WorldToAnimatorPosition(*owner, target.worldPosition);
        const Quat targetRotation =
            WorldToAnimatorRotation(*owner, target.worldRotation);
        const std::size_t constrained = constraint.constrainedBoneIndex;
        switch (definition.type) {
        case AnimatorRigConstraintType::TwoBoneIK: {
            const std::size_t mid = constraint.midBoneIndex;
            const std::size_t tip = constraint.tipBoneIndex;
            const Vec3 rootPosition = pose.component.positions[constrained];
            Vec3 midPosition = pose.component.positions[mid];
            Vec3 tipPosition = pose.component.positions[tip];
            const float upperLength = kb::math::Length(midPosition - rootPosition);
            const float lowerLength = kb::math::Length(tipPosition - midPosition);
            if (upperLength <= 1.0e-5F || lowerLength <= 1.0e-5F) {
                throw std::runtime_error("TwoBoneIK requires non-zero bone lengths");
            }
            const float positionWeight =
                definition.weight * target.positionWeight;
            if (positionWeight > 0.0F) {
                Vec3 delta = targetPosition - rootPosition;
                float distance = kb::math::Length(delta);
                if (distance <= 1.0e-5F) {
                    throw std::runtime_error(
                        "TwoBoneIK target cannot coincide with its root");
                }
                const Vec3 direction = delta * (1.0F / distance);
                distance = std::clamp(
                    distance, std::abs(upperLength - lowerLength) + 1.0e-5F,
                    upperLength + lowerLength - 1.0e-5F);
                const auto poleIt = definition.poleTarget.empty()
                    ? record.ikTargets.end()
                    : record.ikTargets.find(definition.poleTarget);
                const Vec3 poleDirection = poleIt == record.ikTargets.end()
                    ? midPosition - rootPosition
                    : WorldToAnimatorPosition(
                          *owner, poleIt->second.worldPosition) - rootPosition;
                const Vec3 bend = StableBendDirection(
                    direction, poleDirection, midPosition - rootPosition);
                const float cosine = std::clamp(
                    (upperLength * upperLength + distance * distance -
                     lowerLength * lowerLength) /
                        (2.0F * upperLength * distance), -1.0F, 1.0F);
                const Vec3 desiredMid = rootPosition +
                    direction * (cosine * upperLength) +
                    bend * (std::sqrt(std::max(
                        0.0F, 1.0F - cosine * cosine)) * upperLength);
                const Quat rootRotation = kb::math::Normalize(
                    kb::math::FromToRotation(
                        midPosition - rootPosition,
                        desiredMid - rootPosition) *
                    pose.component.rotations[constrained]);
                SetBoneComponentPose(
                    skeleton, pose, constrained, rootPosition,
                    kb::math::Slerp(
                        pose.component.rotations[constrained], rootRotation,
                        positionWeight), false);
                midPosition = pose.component.positions[mid];
                tipPosition = pose.component.positions[tip];
                const Quat midRotation = kb::math::Normalize(
                    kb::math::FromToRotation(
                        tipPosition - midPosition,
                        targetPosition - midPosition) *
                    pose.component.rotations[mid]);
                SetBoneComponentPose(
                    skeleton, pose, mid, midPosition,
                    kb::math::Slerp(
                        pose.component.rotations[mid], midRotation,
                        positionWeight), false);
            }
            const float rotationWeight =
                definition.weight * target.rotationWeight;
            if (rotationWeight > 0.0F) {
                SetBoneComponentPose(
                    skeleton, pose, tip, pose.component.positions[tip],
                    kb::math::Slerp(
                        pose.component.rotations[tip], targetRotation,
                        rotationWeight), false);
            }
            break;
        }
        case AnimatorRigConstraintType::Aim: {
            const float weight = definition.weight * target.positionWeight;
            if (weight <= 0.0F) break;
            const Vec3 direction =
                targetPosition - pose.component.positions[constrained];
            if (kb::math::Dot(direction, direction) <= 1.0e-8F) {
                throw std::runtime_error(
                    "Aim target cannot coincide with its constrained bone");
            }
            const Quat desired = kb::math::LookRotation(
                direction, kb::math::Rotate(
                    targetRotation, Vec3{ 0.0F, 1.0F, 0.0F }));
            SetBoneComponentPose(
                skeleton, pose, constrained,
                pose.component.positions[constrained],
                kb::math::Slerp(
                    pose.component.rotations[constrained], desired, weight),
                false);
            break;
        }
        case AnimatorRigConstraintType::CopyTransform: {
            const float positionWeight =
                definition.weight * target.positionWeight;
            const float rotationWeight =
                definition.weight * target.rotationWeight;
            if (positionWeight <= 0.0F && rotationWeight <= 0.0F) break;
            SetBoneComponentPose(
                skeleton, pose, constrained,
                pose.component.positions[constrained] +
                    (targetPosition - pose.component.positions[constrained]) *
                        positionWeight,
                kb::math::Slerp(
                    pose.component.rotations[constrained], targetRotation,
                    rotationWeight), true);
            break;
        }
        }
    }
}

void ApplyRigConstraints(Scene& scene, AnimatorRuntimeRecord& record) {
    if (record.rigConstraints.empty() || record.skeleton.has_value()) return;
    scene.Runtime().SynchronizeTransforms();
    for (const AnimatorRuntimeConstraint& constraint : record.rigConstraints) {
        if (!scene.Entities().IsActive(constraint.constrained)) continue;
        const AnimatorRigConstraint& definition =
            record.controller->rigConstraints[constraint.definitionIndex];
        const auto targetIt = record.ikTargets.find(definition.target);
        if (targetIt == record.ikTargets.end()) continue;
        const AnimatorIkTarget& target = targetIt->second;
        switch (definition.type) {
        case AnimatorRigConstraintType::TwoBoneIK: {
            const auto poleIt = definition.poleTarget.empty()
                ? record.ikTargets.end()
                : record.ikTargets.find(definition.poleTarget);
            ApplyTwoBoneIk(
                scene, constraint, definition, target,
                poleIt == record.ikTargets.end() ? nullptr : &poleIt->second);
            break;
        }
        case AnimatorRigConstraintType::Aim: {
            const float aimWeight =
                definition.weight * target.positionWeight;
            if (aimWeight <= 0.0F) break;
            const TransformComponent* transform =
                scene.Transforms().TryGet(constraint.constrained);
            if (transform == nullptr) {
                throw std::runtime_error("Aim constraint lost its Transform");
            }
            const Vec3 direction =
                target.worldPosition - transform->worldPosition;
            if (kb::math::Dot(direction, direction) <= 1.0e-8F) {
                throw std::runtime_error(
                    "Aim target cannot coincide with its constrained Transform");
            }
            const Quat desired = kb::math::LookRotation(
                direction,
                kb::math::Rotate(
                    target.worldRotation, Vec3{ 0.0F, 1.0F, 0.0F }));
            SetWorldPose(
                scene, constraint.constrained, transform->worldPosition,
                kb::math::Slerp(
                    transform->worldRotation, desired,
                    aimWeight),
                false);
            scene.Runtime().SynchronizeTransforms();
            break;
        }
        case AnimatorRigConstraintType::CopyTransform: {
            const float positionWeight =
                definition.weight * target.positionWeight;
            const float rotationWeight =
                definition.weight * target.rotationWeight;
            if (positionWeight <= 0.0F && rotationWeight <= 0.0F) break;
            const TransformComponent* transform =
                scene.Transforms().TryGet(constraint.constrained);
            if (transform == nullptr) {
                throw std::runtime_error(
                    "CopyTransform constraint lost its Transform");
            }
            SetWorldPose(
                scene, constraint.constrained,
                transform->worldPosition +
                    (target.worldPosition - transform->worldPosition) *
                        positionWeight,
                kb::math::Slerp(
                    transform->worldRotation, target.worldRotation,
                    rotationWeight),
                true);
            scene.Runtime().SynchronizeTransforms();
            break;
        }
        }
    }
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
    layer.currentTimeSeconds =
        StateTime(normalizedTime, ReferenceClip(layer.states[stateIndex]));
    AdvanceTime(
        layer.currentTimeSeconds, 0.0, ReferenceClip(layer.states[stateIndex]));
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
        const float duration =
            ReferenceClip(layer.states[layer.currentState]).durationSeconds;
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
    candidate.controllerLoadGeneration =
        manager.LoadGeneration(kb::assets::AssetId{ controllerAssetId });
    SceneState& sceneState = SceneAccess::State(scene);
    if (sceneState.nextAnimatorRuntimeBindingGeneration ==
        std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(
            "Animator runtime binding generation exhausted");
    }
    candidate.runtimeBindingGeneration =
        sceneState.nextAnimatorRuntimeBindingGeneration++;
    candidate.observedHierarchyTopologyVersion =
        sceneState.hierarchyTopologyVersion;
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
            AnimatorRuntimeState state{};
            std::vector<std::string_view> references;
            if (!stateDefinition.clipReference.empty()) {
                references.push_back(stateDefinition.clipReference);
            } else {
                for (const AnimatorControllerState::BlendChild& child :
                     stateDefinition.blendChildren) {
                    references.push_back(child.clipReference);
                }
            }
            for (const std::string_view reference : references) {
                auto clip =
                    manager.Load<AnimationClip>(ResolveClip(manager, reference));
                if (!clip.IsLoaded()) return false;
                if (!state.motions.empty() &&
                    (clip->durationSeconds !=
                         state.motions.front().clip->durationSeconds ||
                     clip->looping != state.motions.front().clip->looping)) {
                    return false;
                }
                AnimatorRuntimeState::Motion motion{};
                motion.clip = std::move(clip);
                motion.clipLoadGeneration =
                    manager.LoadGeneration(motion.clip.Id());
                if (motion.clip->targetSkeletonAssetId != 0U) {
                    if (!BindSkeletalMotion(scene, candidate, motion)) {
                        return false;
                    }
                } else {
                    if (candidate.skeleton.has_value()) return false;
                    motion.targetIndices.reserve(motion.clip->tracks.size());
                    for (const AnimationTransformTrack& track :
                         motion.clip->tracks) {
                        const SceneEntity target =
                            ResolveTarget(scene, entity, track.targetPath);
                        const TransformComponent* transform =
                            scene.Transforms().TryGet(target);
                        if (!target.IsValid() || transform == nullptr) {
                            return false;
                        }
                        auto binding = std::find_if(
                            candidate.bindings.begin(),
                            candidate.bindings.end(),
                            [target](const AnimatorRuntimeBinding& value) {
                                return value.target == target;
                            });
                        if (binding == candidate.bindings.end()) {
                            candidate.bindings.push_back(
                                AnimatorRuntimeBinding{
                                    .target = target,
                                    .bindTransform =
                                        transform->LocalPayload(),
                                });
                            binding = candidate.bindings.end() - 1;
                        }
                        motion.targetIndices.push_back(
                            static_cast<std::size_t>(
                                binding - candidate.bindings.begin()));
                    }
                }
                state.motions.push_back(std::move(motion));
            }
            if (state.motions.empty()) return false;
            if (!stateDefinition.blendChildren.empty()) {
                const auto parameter = std::find_if(
                    controller->parameters.begin(), controller->parameters.end(),
                    [&](const AnimatorParameterDefinition& value) {
                        return value.name == stateDefinition.blendParameter &&
                            value.type == AnimatorParameterType::Float;
                    });
                if (parameter == controller->parameters.end()) return false;
                state.blendParameterIndex = static_cast<std::size_t>(
                    parameter - controller->parameters.begin());
            }
            layer.states.push_back(std::move(state));
        }
        layer.currentState = StateIndex(layerDefinition, layerDefinition.defaultState);
        if (layer.currentState >= layer.states.size()) return false;
        layer.previousState = layer.currentState;
        candidate.layers.push_back(std::move(layer));
    }
    if (!BindMorphTargets(candidate)) return false;
    if (candidate.skeleton.has_value()) {
        const AnimatorRuntimeState::Motion& rootMotion =
            candidate.layers.front().states.front().motions.front();
        for (const AnimatorRuntimeState& runtimeState :
             candidate.layers.front().states) {
            for (const AnimatorRuntimeState::Motion& motion :
                 runtimeState.motions) {
                if (motion.clip->rootMotionMode !=
                        rootMotion.clip->rootMotionMode ||
                    motion.clip->rootMotionBoneId !=
                        rootMotion.clip->rootMotionBoneId ||
                    (motion.clip->rootMotionMode ==
                         AnimationRootMotionMode::ExtractFromBone &&
                     SkeletalRootTrack(
                         motion, controller->layers.front()) == nullptr)) {
                    return false;
                }
            }
        }
    }
    candidate.rigConstraints.reserve(controller->rigConstraints.size());
    for (std::size_t definitionIndex = 0U;
         definitionIndex < controller->rigConstraints.size();
         ++definitionIndex) {
        const AnimatorRigConstraint& definition =
            controller->rigConstraints[definitionIndex];
        AnimatorRuntimeConstraint constraint{};
        constraint.definitionIndex = definitionIndex;
        if (candidate.skeleton.has_value()) {
            if (definition.constrainedBoneId == 0U) return false;
            const auto constrained = candidate.skeleton->boneIndices.find(
                definition.constrainedBoneId);
            if (constrained == candidate.skeleton->boneIndices.end()) {
                return false;
            }
            constraint.constrainedBoneIndex = constrained->second;
            if (definition.type == AnimatorRigConstraintType::TwoBoneIK) {
                const auto mid = candidate.skeleton->boneIndices.find(
                    definition.midBoneId);
                const auto tip = candidate.skeleton->boneIndices.find(
                    definition.tipBoneId);
                if (mid == candidate.skeleton->boneIndices.end() ||
                    tip == candidate.skeleton->boneIndices.end() ||
                    candidate.skeleton->asset->bones[mid->second].parentIndex !=
                        static_cast<std::int32_t>(constraint.constrainedBoneIndex) ||
                    candidate.skeleton->asset->bones[tip->second].parentIndex !=
                        static_cast<std::int32_t>(mid->second)) return false;
                constraint.midBoneIndex = mid->second;
                constraint.tipBoneIndex = tip->second;
            }
        } else {
            constraint.constrained = ResolveTarget(
                scene, entity, definition.constrainedPath);
            if (!constraint.constrained.IsValid()) return false;
            if (definition.type == AnimatorRigConstraintType::TwoBoneIK) {
                constraint.mid = ResolveTarget(scene, entity, definition.midPath);
                constraint.tip = ResolveTarget(scene, entity, definition.tipPath);
                if (!constraint.mid.IsValid() || !constraint.tip.IsValid() ||
                    scene.Hierarchy().Parent(constraint.mid) != constraint.constrained ||
                    scene.Hierarchy().Parent(constraint.tip) != constraint.mid) {
                    return false;
                }
            }
        }
        candidate.rigConstraints.push_back(std::move(constraint));
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

std::uint64_t SceneAnimatorService::RuntimeBindingGeneration(
    const Scene& scene, SceneEntity entity) noexcept {
    const AnimatorRuntimeRecord* record =
        Find(SceneAccess::State(scene), entity);
    return record == nullptr ? 0U : record->runtimeBindingGeneration;
}

std::optional<AnimatorInstanceSkeletonView>
SceneAnimatorService::InstanceSkeleton(
    const Scene& scene, SceneEntity entity) noexcept {
    const AnimatorRuntimeRecord* instance =
        Find(SceneAccess::State(scene), entity);
    if (instance == nullptr || !instance->skeleton.has_value()) {
        return std::nullopt;
    }
    const AnimatorInstanceSkeleton& skeleton = *instance->skeleton;
    const AnimatorInstanceSkeleton::PoseBuffer& current =
        skeleton.poses[skeleton.currentPose];
    const AnimatorInstanceSkeleton::PoseBuffer& previous =
        skeleton.poses[skeleton.currentPose ^ 1U];
    const auto view = [](const AnimatorInstanceSkeleton::PoseSoa& pose) {
        return AnimatorPoseSoaView{
            .positions = pose.positions,
            .rotations = pose.rotations,
            .scales = pose.scales,
        };
    };
    return AnimatorInstanceSkeletonView{
        .skeletonAssetId = skeleton.asset.Id().value,
        .compatibilitySignature =
            skeleton.compatibilitySignature,
        .boneIds = skeleton.boneIds,
        .currentLocalPose = view(current.local),
        .previousLocalPose = view(previous.local),
        .currentComponentPose = view(current.component),
        .previousComponentPose = view(previous.component),
        .currentSkinMatrices = current.skinMatrices,
        .previousSkinMatrices = previous.skinMatrices,
        .morphWeights = {
            .targetNames = skeleton.morphTargetNames,
            .currentWeights = skeleton.morphWeights[skeleton.currentPose],
            .previousWeights = skeleton.morphWeights[skeleton.currentPose ^ 1U],
        },
        .evaluationCount = skeleton.evaluationCount,
        .hierarchySolveCount = skeleton.hierarchySolveCount,
    };
}

std::optional<AnimatorAttachmentTransform>
SceneAnimatorService::AttachmentTransform(
    const Scene& scene, SceneEntity entity, SkeletonBoneId boneId,
    const LocalTransform& localOffset) noexcept {
    if (boneId == 0U || !IsFiniteTransform(localOffset)) return std::nullopt;
    const AnimatorRuntimeRecord* instance = Find(SceneAccess::State(scene), entity);
    const TransformComponent* owner = scene.Transforms().TryGet(entity);
    if (instance == nullptr || !instance->skeleton.has_value() || owner == nullptr) {
        return std::nullopt;
    }
    const AnimatorInstanceSkeleton& skeleton = *instance->skeleton;
    const auto bone = std::find(skeleton.boneIds.begin(), skeleton.boneIds.end(), boneId);
    if (bone == skeleton.boneIds.end()) return std::nullopt;
    const std::size_t index = static_cast<std::size_t>(bone - skeleton.boneIds.begin());
    const AnimatorInstanceSkeleton::PoseBuffer& pose = skeleton.poses[skeleton.currentPose];
    if (index >= pose.component.positions.size() || index >= pose.component.rotations.size() ||
        index >= pose.component.scales.size()) {
        return std::nullopt;
    }
    const LocalTransform component = ComposeTransform({
        .position = pose.component.positions[index],
        .rotation = pose.component.rotations[index],
        .scale = pose.component.scales[index],
    }, localOffset);
    return AnimatorAttachmentTransform{
        .componentSpace = component,
        .worldSpace = ComposeWorldTransform(owner->WorldPayload(), component),
    };
}

std::optional<AnimatorAttachmentTransform>
SceneAnimatorService::SocketTransform(
    const Scene& scene, SceneEntity entity, std::string_view socketName) noexcept {
    if (socketName.empty()) return std::nullopt;
    const AnimatorRuntimeRecord* instance = Find(SceneAccess::State(scene), entity);
    if (instance == nullptr || !instance->skeleton.has_value()) return std::nullopt;
    const SkeletonAsset& skeleton = *instance->skeleton->asset;
    const auto socket = std::find_if(
        skeleton.sockets.begin(), skeleton.sockets.end(),
        [socketName](const SkeletonSocket& value) { return value.name == socketName; });
    return socket == skeleton.sockets.end()
        ? std::nullopt
        : AttachmentTransform(scene, entity, socket->boneId, socket->localTransform);
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
    layer.currentTimeSeconds =
        StateTime(normalizedTime, ReferenceClip(layer.states[stateIndex]));
    AdvanceTime(
        layer.currentTimeSeconds, 0.0, ReferenceClip(layer.states[stateIndex]));
    layer.previousTimeSeconds = layer.currentTimeSeconds;
    layer.transitioning = false;
    layer.transitionElapsedSeconds = 0.0;
    layer.transitionDurationSeconds = 0.0;
    return true;
}

bool SceneAnimatorService::SeekNormalized(
    Scene& scene, SceneEntity entity, float normalizedTime) noexcept {
    if (!std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F) return false;
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr || record->layers.size() != record->controller->layers.size()) return false;
    for (std::size_t layerIndex = 0U; layerIndex < record->layers.size(); ++layerIndex) {
        AnimatorRuntimeLayer& layer = record->layers[layerIndex];
        if (layer.currentState >= layer.states.size()) return false;
        const AnimationClip& clip = ReferenceClip(layer.states[layer.currentState]);
        layer.currentTimeSeconds = StateTime(normalizedTime, clip);
        layer.previousState = layer.currentState;
        layer.previousTimeSeconds = layer.currentTimeSeconds;
        layer.transitioning = false;
        layer.transitionElapsedSeconds = 0.0;
        layer.transitionDurationSeconds = 0.0;
    }
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

float SceneAnimatorService::CurrentStateDuration(const Scene& scene, SceneEntity entity) noexcept {
    const AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr || record->layers.empty()) return 0.0F;
    const AnimatorRuntimeLayer& layer = record->layers.front();
    if (layer.currentState >= layer.states.size()) return 0.0F;
    return ReferenceClip(layer.states[layer.currentState]).durationSeconds;
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

bool SceneAnimatorService::SetIkTarget(
    Scene& scene, SceneEntity entity, std::string_view name,
    const AnimatorIkTarget& target) noexcept {
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr || name.empty() || !IsFinite(target)) return false;
    const bool declared = std::any_of(
        record->controller->rigConstraints.begin(),
        record->controller->rigConstraints.end(),
        [name](const AnimatorRigConstraint& constraint) {
            return constraint.target == name || constraint.poleTarget == name;
        });
    if (!declared) return false;
    AnimatorIkTarget normalized = target;
    normalized.worldRotation =
        kb::math::Normalize(normalized.worldRotation);
    record->ikTargets.insert_or_assign(std::string{ name }, normalized);
    return true;
}

bool SceneAnimatorService::ClearIkTarget(
    Scene& scene, SceneEntity entity, std::string_view name) noexcept {
    AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return false;
    const auto target = record->ikTargets.find(name);
    if (target == record->ikTargets.end()) return false;
    record->ikTargets.erase(target);
    return true;
}

std::optional<AnimatorStateInfo> SceneAnimatorService::State(const Scene& scene, SceneEntity entity, std::string_view layerName) {
    const AnimatorRuntimeRecord* record = Find(SceneAccess::State(scene), entity);
    if (record == nullptr) return std::nullopt;
    const std::size_t layerIndex = LayerIndex(*record->controller, layerName);
    if (layerIndex >= record->layers.size()) return std::nullopt;
    const AnimatorRuntimeLayer& layer = record->layers[layerIndex];
    const AnimatorControllerLayer& definition = record->controller->layers[layerIndex];
    const float duration =
        ReferenceClip(layer.states[layer.currentState]).durationSeconds;
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

std::shared_ptr<const AnimatorDebugSnapshot> SceneAnimatorService::DebugSnapshot(
    const Scene& scene) noexcept {
    return SceneAccess::State(scene).animatorDebugSnapshots.Read();
}

void SceneAnimatorService::PublishDebugSnapshot(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    BuildAndPublishDebugSnapshot(scene, state);
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
        SceneState& state = SceneAccess::State(scene);
        const kb::assets::AssetId controllerAssetId{
            value.animator.controllerAssetId
        };
        const bool controllerChanged =
            record == nullptr ||
            record->controller.Id() != controllerAssetId ||
            record->controllerLoadGeneration !=
                scene.Assets().Manager().LoadGeneration(controllerAssetId);
        const bool controllerReloaded =
            record != nullptr &&
            record->controller.Id() == controllerAssetId &&
            record->controllerLoadGeneration !=
                scene.Assets().Manager().LoadGeneration(controllerAssetId);
        const bool clipChanged =
            record != nullptr &&
            !RuntimeAssetsAreCurrent(scene.Assets().Manager(), *record);
        const bool skeletalBindingChanged =
            record != nullptr &&
            !RuntimeSkeletalBindingIsCurrent(scene, *record);
        const bool hierarchyChanged =
            record != nullptr &&
            record->observedHierarchyTopologyVersion !=
                state.hierarchyTopologyVersion;
        const bool hierarchyBindingsChanged =
            hierarchyChanged &&
            !RuntimeBindingsMatchCanonicalHierarchy(scene, *record);
        if (controllerChanged || clipChanged || skeletalBindingChanged ||
            hierarchyBindingsChanged) {
            // Once a canonical source or binding is stale, the old derived
            // record must not remain queryable or feed a physics queue if the
            // replacement asset fails to load.
            std::optional<AnimatorRuntimeRecord> previous;
            if (record != nullptr) {
                previous.emplace(std::move(*record));
            }
            state.animators.erase(value.entity.Id());
            if (!Attach(scene, value.entity, value.animator.controllerAssetId)) {
                throw std::runtime_error("Animator component could not load its controller or bind the authored hierarchy");
            }
            record = Find(SceneAccess::State(scene), value.entity);
            if (previous.has_value() &&
                (controllerReloaded || clipChanged ||
                 skeletalBindingChanged)) {
                RestoreCompatibleRuntimeState(*previous, *record);
            }
            record->lastAppliedComponentSpeed = value.animator.speed;
            record->speed = value.animator.speed;
        } else if (hierarchyChanged) {
            record->observedHierarchyTopologyVersion =
                state.hierarchyTopologyVersion;
        }
        if (record->lastAppliedComponentSpeed != value.animator.speed) {
            record->lastAppliedComponentSpeed = value.animator.speed;
            record->speed = value.animator.speed;
        }
        if (!state.isPlaying) {
            for (AnimatorRuntimeBinding& binding : record->bindings) {
                const TransformComponent* transform =
                    scene.Transforms().TryGet(binding.target);
                if (transform == nullptr) {
                    throw std::runtime_error(
                        "Animator binding lost its Transform while playback was paused");
                }
                binding.bindTransform = transform->LocalPayload();
            }
        }
    }
    SceneState& state = SceneAccess::State(scene);
    for (auto it = state.animators.begin(); it != state.animators.end();) {
        const Animator* component = state.componentStorage.Animators().TryGet(it->second.entity);
        if (component == nullptr || !component->enabled || !retained.contains(it->first)) {
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
            const AnimatorControllerLayer& definition =
                record.controller->layers[static_cast<std::size_t>(
                    &layer - record.layers.data())];
            const std::size_t currentCount = EventCrossingCount(
                record, layer.states[layer.currentState],
                definition.states[layer.currentState],
                layer.currentTimeSeconds, scaledDelta, available);
            if (currentCount > available) {
                throw std::length_error("AnimatorSceneSystem exceeded the pending animation-event capacity");
            }
            eventCount += currentCount;
            if (layer.transitioning) {
                const std::size_t remaining = SceneAnimators::kMaxPendingEvents - eventCount;
                const std::size_t previousCount = EventCrossingCount(
                    record, layer.states[layer.previousState],
                    definition.states[layer.previousState],
                    layer.previousTimeSeconds, scaledDelta, remaining);
                if (previousCount > remaining) {
                    throw std::length_error("AnimatorSceneSystem exceeded the pending animation-event capacity");
                }
                eventCount += previousCount;
            }
        }
    }
    state.pendingAnimationEvents.reserve(eventCount);

    for (auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (!scene.Entities().IsActive(record.entity)) continue;
        record.hasExtractedRootMotion = false;
        record.extractedRootMotionTranslation = {};
        record.extractedRootMotionRotation = {};
        const Animator* component = scene.Components().Animators().TryGet(record.entity);
        if (component == nullptr || component->rootMotionOwner == AnimatorRootMotionOwner::None) continue;
        const double scaledDelta = static_cast<double>(deltaSeconds) * static_cast<double>(record.speed);
        const RootMotionDelta motion = ExtractRootMotion(record, scaledDelta, deltaSeconds);
        record.extractedRootMotionTranslation = motion.translation;
        record.extractedRootMotionRotation = motion.rotation;
        record.hasExtractedRootMotion = true;
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
            rootMotionOwner == AnimatorRootMotionOwner::Animator &&
                record.hasExtractedRootMotion
            ? RootMotionDelta{
                  .translation = record.extractedRootMotionTranslation,
                  .rotation = record.extractedRootMotionRotation,
              }
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
                    record, layer.states[layer.previousState],
                    definition.states[layer.previousState],
                    layer.previousTimeSeconds, scaledDelta, record.entity,
                    definition.name, definition.states[layer.previousState].name, state.pendingAnimationEvents);
            }
            QueueStateEvents(
                record, layer.states[layer.currentState],
                definition.states[layer.currentState],
                layer.currentTimeSeconds, scaledDelta, record.entity,
                definition.name, definition.states[layer.currentState].name, state.pendingAnimationEvents);
            AdvanceTime(
                layer.currentTimeSeconds, scaledDelta,
                ReferenceClip(layer.states[layer.currentState]));
            if (layer.transitioning) {
                AdvanceTime(
                    layer.previousTimeSeconds, scaledDelta,
                    ReferenceClip(layer.states[layer.previousState]));
                layer.transitionElapsedSeconds = std::min(layer.transitionElapsedSeconds + static_cast<double>(deltaSeconds), layer.transitionDurationSeconds);
                for (AnimatorRuntimeBinding& binding : record.bindings) {
                    binding.fromTouched = false;
                    binding.toTouched = false;
                }
                EvaluateState(
                    record, layer.states[layer.previousState],
                    definition.states[layer.previousState],
                    layer.previousTimeSeconds, definition.mask, true);
                EvaluateState(
                    record, layer.states[layer.currentState],
                    definition.states[layer.currentState],
                    layer.currentTimeSeconds, definition.mask, false);
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
                for (AnimatorRuntimeBinding& binding : record.bindings) {
                    binding.toTouched = false;
                }
                EvaluateState(
                    record, runtimeState,
                    definition.states[layer.currentState],
                    layer.currentTimeSeconds, definition.mask, false);
                for (AnimatorRuntimeBinding& binding : record.bindings) {
                    if (!binding.toTouched) continue;
                    binding.output =
                        Blend(binding.output, binding.toPose, definition.weight);
                    binding.outputTouched = true;
                }
            }
        }
        if (record.skeleton.has_value()) {
            std::optional<std::uint32_t> extractedRootMotionBone;
            if (record.hasExtractedRootMotion) {
                const AnimatorRuntimeLayer& rootLayer =
                    record.layers.front();
                const std::uint32_t boneIndex = rootLayer
                    .states[rootLayer.currentState].motions.front()
                    .rootMotionBoneIndex;
                if (boneIndex ==
                    std::numeric_limits<std::uint32_t>::max()) {
                    throw std::runtime_error(
                        "Skeletal root motion has no prebound source bone");
                }
                extractedRootMotionBone = boneIndex;
            }
            EvaluateIndexedSkeletalPose(
                scene, record, extractedRootMotionBone);
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
    for (auto& [id, record] : state.animators) {
        static_cast<void>(id);
        if (scene.Entities().IsActive(record.entity)) {
            const Animator* component =
                scene.Components().Animators().TryGet(record.entity);
            if (component != nullptr &&
                component->rootMotionOwner != AnimatorRootMotionOwner::None &&
                std::any_of(
                    record.rigConstraints.begin(), record.rigConstraints.end(),
                    [&](const AnimatorRuntimeConstraint& constraint) {
                        return constraint.constrained == record.entity;
                    })) {
                throw std::runtime_error(
                    "Rig constraints cannot drive the root-motion owner Transform");
            }
            ApplyRigConstraints(scene, record);
        }
    }
}

} // namespace kb::scene

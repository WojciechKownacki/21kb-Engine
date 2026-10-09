#include "scene/SceneHistoryService.hpp"

#include "engine/ecs/World.hpp"
#include "engine/scene/JointComponent.hpp"
#include "engine/scene/LensEchoComponent.hpp"
#include "engine/scene/RegionPortalComponent.hpp"
#include "engine/scene/SceneAudioMixerAccess.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneJointComponents.hpp"
#include "engine/scene/SceneUIComponentSet.hpp"
#include "engine/ui/UIComponentSet.hpp"
#include "engine/ui/UIEntityReferences.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneComponentQueryService.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/SceneTransformService.hpp"
#include "scene/hierarchy/SceneHierarchyCache.hpp"
#include "scene/prefab/ScenePrefabComponentSnapshot.hpp"
#include "scene/prefab/ScenePrefabDirtyTracker.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabNodeStateWriter.hpp"
#include "scene/prefab/ScenePrefabReferenceResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

using EntityId = SceneEntity::IdType;
constexpr std::uint32_t kNoSiblingIndex = SceneHistoryObjectChange::NoSiblingIndex;

[[nodiscard]] EntityId SiblingsKey(SceneEntity parent) noexcept {
    return parent.IsValid() ? parent.Id() : 0U;
}

[[nodiscard]] std::vector<SceneEntity> Siblings(const SceneState& state, SceneEntity parent) {
    return parent.IsValid() ? SceneHierarchyCache::Children(state, parent) : SceneHierarchyCache::Roots(state);
}

[[nodiscard]] SceneHistorySettings CaptureSettings(Scene& scene) {
    return SceneHistorySettings{
        .audioMixerAssetId = SceneAudioMixerAccess::ActiveMixer(scene),
        .audioMixerSnapshot = SceneAudioMixerAccess::ActiveSnapshot(scene),
        .audioOcclusionSettings = SceneAudioOcclusionAccess::Settings(scene),
    };
}

[[nodiscard]] bool ApplySettings(Scene& scene, const SceneHistorySettings& settings) {
    SceneAudioMixerAccess::SetActiveMixer(scene, settings.audioMixerAssetId);
    if (!SceneAudioMixerAccess::SetActiveSnapshot(scene, settings.audioMixerSnapshot)
        || !SceneAudioOcclusionAccess::Configure(scene, settings.audioOcclusionSettings)) {
        return false;
    }
    SceneAudioMixerAccess::ResetRuntimeMixerState(scene);
    return true;
}

[[nodiscard]] SceneHistoryObjectState CaptureObject(Scene& scene, SceneEntity entity) {
    SceneHistoryObjectState state;
    if (!entity.IsValid() || !SceneEntityService::IsAlive(scene, entity)) {
        return state;
    }
    const SceneObject object = SceneAccess::MakeObject(scene, entity);
    state.alive = true;
    state.parent = SceneHierarchyService::Parent(scene, entity);
    state.node.name = SceneEntityService::Name(scene, entity);
    state.node.transform = SceneTransformService::Get(scene, entity);
    state.node.SetLocalTranslation(scene.Transforms().LocalTranslation(entity));
    state.node.visibility = SceneComponentQueryService::Visibility(scene, entity);
    state.node.components = ScenePrefabComponentSnapshot::Capture(scene, object);
    state.active = SceneEntityService::IsActive(scene, entity);
    state.persistent = SceneEntityService::IsPersistent(scene, entity);
    const std::span<const BehaviourVariableOverride> overrides = SceneEntityService::BehaviourVariableOverrides(scene, entity);
    state.behaviourVariableOverrides.assign(overrides.begin(), overrides.end());
    return state;
}

// Byte equality of the stored values: a difference in padding can only report a change that did not happen.
template <typename T>
[[nodiscard]] bool SameValue(const std::optional<T>& lhs, const std::optional<T>& rhs) noexcept {
    static_assert(std::is_trivially_copyable_v<T>, "history compares component values byte for byte");
    if (lhs.has_value() != rhs.has_value()) {
        return false;
    }
    return !lhs.has_value() || std::memcmp(&*lhs, &*rhs, sizeof(T)) == 0;
}

// Every member of ScenePrefabNodeComponents takes part: a member left out here would let a command that only
// changed it be dropped as a no-op.
[[nodiscard]] bool SameComponents(const ScenePrefabNodeComponents& lhs, const ScenePrefabNodeComponents& rhs) noexcept {
    return AreUIComponentSetsEqual(lhs.ui, rhs.ui)
        && SameValue(lhs.camera, rhs.camera)
        && SameValue(lhs.meshRenderer, rhs.meshRenderer)
        && SameValue(lhs.light, rhs.light)
        && SameValue(lhs.input, rhs.input)
        && SameValue(lhs.rigidbody, rhs.rigidbody)
        && SameValue(lhs.collider, rhs.collider)
        && SameValue(lhs.characterController, rhs.characterController)
        && SameValue(lhs.joint, rhs.joint)
        && SameValue(lhs.tags, rhs.tags)
        && SameValue(lhs.regionShape, rhs.regionShape)
        && SameValue(lhs.guideCurve, rhs.guideCurve)
        && SameValue(lhs.contentInstance, rhs.contentInstance)
        && SameValue(lhs.streamFocus, rhs.streamFocus)
        && SameValue(lhs.worldBackdrop, rhs.worldBackdrop)
        && SameValue(lhs.ambientRadiance, rhs.ambientRadiance)
        && SameValue(lhs.detailSwitch, rhs.detailSwitch)
        && SameValue(lhs.visibilityBlocker, rhs.visibilityBlocker)
        && SameValue(lhs.visibilityCell, rhs.visibilityCell)
        && SameValue(lhs.regionPortal, rhs.regionPortal)
        && SameValue(lhs.auxFrame, rhs.auxFrame)
        && SameValue(lhs.geometrySwarm, rhs.geometrySwarm)
        && SameValue(lhs.surfaceCast, rhs.surfaceCast)
        && SameValue(lhs.facingPanel, rhs.facingPanel)
        && SameValue(lhs.spaceStroke, rhs.spaceStroke)
        && SameValue(lhs.historyRibbon, rhs.historyRibbon)
        && SameValue(lhs.particleEffect, rhs.particleEffect)
        && SameValue(lhs.lensEcho, rhs.lensEcho)
        && SameValue(lhs.behaviour, rhs.behaviour)
        && SameValue(lhs.audioSource, rhs.audioSource)
        && SameValue(lhs.audioListener, rhs.audioListener)
        && SameValue(lhs.animator, rhs.animator)
        && SameValue(lhs.skeletonBinding, rhs.skeletonBinding)
        && SameValue(lhs.motionSkeletonRule, rhs.motionSkeletonRule)
        && SameValue(lhs.deformedGeometry, rhs.deformedGeometry)
        && SameValue(lhs.navAgent, rhs.navAgent)
        && SameValue(lhs.navObstacle, rhs.navObstacle);
}

[[nodiscard]] bool SameVec3(const Vec3& lhs, const Vec3& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
}

[[nodiscard]] bool SameState(const SceneHistoryObjectState& lhs, const SceneHistoryObjectState& rhs) noexcept {
    if (lhs.alive != rhs.alive) {
        return false;
    }
    if (!lhs.alive) {
        return true;
    }
    const TransformComponent& left = lhs.node.transform;
    const TransformComponent& right = rhs.node.transform;
    const bool sameOverrides = std::ranges::equal(lhs.behaviourVariableOverrides, rhs.behaviourVariableOverrides,
        [](const BehaviourVariableOverride& a, const BehaviourVariableOverride& b) noexcept {
            return a.name == b.name && a.value == b.value;
        });
    return lhs.parent == rhs.parent && lhs.node.name == rhs.node.name
        && lhs.node.LocalTranslation() == rhs.node.LocalTranslation() && SameVec3(left.localScale, right.localScale)
        && left.localRotation.x == right.localRotation.x && left.localRotation.y == right.localRotation.y
        && left.localRotation.z == right.localRotation.z && left.localRotation.w == right.localRotation.w
        && lhs.node.visibility.mode == rhs.node.visibility.mode && lhs.node.visibility.mask == rhs.node.visibility.mask
        && lhs.node.visibility.visible == rhs.node.visibility.visible
        && lhs.active == rhs.active && lhs.persistent == rhs.persistent && sameOverrides
        && SameComponents(lhs.node.components, rhs.node.components);
}

template <typename Visit>
void ForEachObjectReference(ScenePrefabNodeComponents& components, Visit&& visit) {
    if (components.joint.has_value()) {
        visit(components.joint->connectedNodeStableId);
    }
    if (components.regionPortal.has_value()) {
        visit(components.regionPortal->sourceCellNodeStableId);
        visit(components.regionPortal->targetCellNodeStableId);
    }
    if (components.lensEcho.has_value()) {
        visit(components.lensEcho->sourceNodeStableId);
    }
    ForEachUIEntityReference(components.ui, visit);
}

template <typename Component>
void CollectReferenceHolders(const kb::ecs::World& world, std::vector<SceneEntity>& holders) {
    world.ForEach<Component>([](kb::ecs::Entity entity, const Component&, void* context) {
        static_cast<std::vector<SceneEntity>*>(context)->push_back(entity);
    }, &holders);
}

// Objects the entry does not cover may still name an object it recreated by the entity id that object had. Only
// the components that can hold such a reference are visited, and each holder naming a recreated object is
// pointed at its replacement.
void RelinkReferences(Scene& scene, const std::unordered_map<EntityId, SceneEntity>& replacements) {
    if (replacements.empty()) {
        return;
    }
    const kb::ecs::World& world = SceneAccess::State(scene).world;
    std::vector<SceneEntity> holders;
    CollectReferenceHolders<JointComponent>(world, holders);
    CollectReferenceHolders<SceneRegionPortalComponent>(world, holders);
    CollectReferenceHolders<LensEchoComponent>(world, holders);
    CollectReferenceHolders<UISelectable>(world, holders);
    CollectReferenceHolders<UIToggle>(world, holders);
    CollectReferenceHolders<UISlider>(world, holders);
    CollectReferenceHolders<UIProgressBar>(world, holders);
    CollectReferenceHolders<UIScrollView>(world, holders);
    CollectReferenceHolders<UIDropdown>(world, holders);
    std::unordered_set<EntityId> visited;
    for (const SceneEntity holder : holders) {
        if (!visited.insert(holder.Id()).second || !SceneEntityService::IsAlive(scene, holder)) {
            continue;
        }
        ScenePrefabNodeDesc node;
        node.components = ScenePrefabComponentSnapshot::Capture(scene, SceneAccess::MakeObject(scene, holder));
        bool relinked = false;
        ScenePrefabReferenceResolver::EntityMap references;
        ForEachObjectReference(node.components, [&replacements, &references, &relinked](std::uint64_t& reference) {
            if (reference == 0U) {
                return;
            }
            if (const auto replacement = replacements.find(reference); replacement != replacements.end()) {
                reference = replacement->second.Id();
                relinked = true;
            }
            references.emplace(reference, SceneEntity{ reference });
        });
        if (relinked) {
            ScenePrefabReferenceResolver::Apply(scene, node, holder, references);
        }
    }
}

// The recording that takes reports now: none while history reads or writes the scene itself.
[[nodiscard]] SceneHistoryRecording* ActiveRecording(SceneState& state) noexcept {
    SceneHistoryRecording* recording = state.historyRecording.get();
    return recording != nullptr && !recording->busy && !recording->failed && recording->thread == std::this_thread::get_id() ? recording : nullptr;
}

class BusyScope {
public:
    explicit BusyScope(SceneHistoryRecording& recording) noexcept
        : recording_(recording) {
        recording_.busy = true;
    }
    ~BusyScope() {
        recording_.busy = false;
    }
    BusyScope(const BusyScope&) = delete;
    BusyScope& operator=(const BusyScope&) = delete;

private:
    SceneHistoryRecording& recording_;
};

// Keeps the state the object has before the command first changes it.
std::size_t Track(Scene& scene, SceneHistoryRecording& recording, SceneEntity entity) {
    if (const auto known = recording.objectIndex.find(entity.Id()); known != recording.objectIndex.end()) {
        return known->second;
    }
    SceneHistoryObjectChange change{ .entity = entity };
    {
        const BusyScope busy{ recording };
        change.before = CaptureObject(scene, entity);
    }
    const std::size_t index = recording.objects.size();
    recording.objects.push_back(std::move(change));
    recording.structural.push_back(false);
    recording.objectIndex.emplace(entity.Id(), index);
    return index;
}

void KeepSiblings(SceneState& state, SceneHistoryRecording& recording, SceneEntity parent) {
    const EntityId key = SiblingsKey(parent);
    if (!recording.siblingsBefore.contains(key)) {
        recording.siblingsBefore.emplace(key, Siblings(state, parent));
    }
}

// Ends the recording command: each object it kept is compared with its state now, and only real changes stay.
void Seal(Scene& scene, SceneState& state) {
    std::unique_ptr<SceneHistoryRecording> recording = std::move(state.historyRecording);
    if (recording == nullptr) {
        return;
    }
    state.prefabInstances.SetJournal(nullptr);
    if (recording->failed) {
        // A state could not be kept, so this command (and everything before it) cannot be undone exactly.
        state.undoHistory.Clear();
        state.redoHistory.Clear();
        return;
    }
    if (state.undoHistory.Empty()) {
        return;
    }

    SceneHistoryEntry& entry = state.undoHistory.Top();
    entry.settingsAfter = CaptureSettings(scene);

    std::unordered_set<EntityId> created;
    for (const SceneHistoryObjectChange& change : recording->objects) {
        if (!change.before.alive) {
            created.insert(change.entity.Id());
        }
    }
    std::unordered_map<EntityId, std::unordered_map<EntityId, std::uint32_t>> beforePositions;
    std::unordered_map<EntityId, std::unordered_map<EntityId, std::uint32_t>> afterPositions;
    const auto beforeIndex = [&](SceneEntity parent, SceneEntity entity) {
        const EntityId key = SiblingsKey(parent);
        auto positions = beforePositions.find(key);
        if (positions == beforePositions.end()) {
            positions = beforePositions.emplace(key, std::unordered_map<EntityId, std::uint32_t>{}).first;
            if (const auto siblings = recording->siblingsBefore.find(key); siblings != recording->siblingsBefore.end()) {
                std::uint32_t position = 0U;
                for (const SceneEntity sibling : siblings->second) {
                    if (!created.contains(sibling.Id())) {
                        positions->second.emplace(sibling.Id(), position++);
                    }
                }
            }
        }
        const auto found = positions->second.find(entity.Id());
        return found == positions->second.end() ? kNoSiblingIndex : found->second;
    };
    const auto afterIndex = [&](SceneEntity parent, SceneEntity entity) {
        const EntityId key = SiblingsKey(parent);
        auto positions = afterPositions.find(key);
        if (positions == afterPositions.end()) {
            positions = afterPositions.emplace(key, std::unordered_map<EntityId, std::uint32_t>{}).first;
            std::uint32_t position = 0U;
            for (const SceneEntity sibling : Siblings(state, parent)) {
                positions->second.emplace(sibling.Id(), position++);
            }
        }
        const auto found = positions->second.find(entity.Id());
        return found == positions->second.end() ? kNoSiblingIndex : found->second;
    };

    entry.objects.reserve(entry.objects.size() + recording->objects.size());
    for (std::size_t index = 0U; index < recording->objects.size(); ++index) {
        SceneHistoryObjectChange& change = recording->objects[index];
        change.after = CaptureObject(scene, change.entity);
        const bool structural = recording->structural[index] || change.before.alive != change.after.alive;
        if (structural) {
            change.beforeSiblingIndex = change.before.alive ? beforeIndex(change.before.parent, change.entity) : kNoSiblingIndex;
            change.afterSiblingIndex = change.after.alive ? afterIndex(change.after.parent, change.entity) : kNoSiblingIndex;
        }
        if (change.beforeSiblingIndex == change.afterSiblingIndex && SameState(change.before, change.after)) {
            continue;
        }
        entry.objects.push_back(std::move(change));
    }

    for (SceneHistoryPrefabInstanceChange& change : recording->prefabInstances) {
        change.after = CaptureSceneHistoryPrefabInstance(state.prefabInstances.Find(change.handle));
        if (change.before.present || change.after.present) {
            entry.prefabInstances.push_back(std::move(change));
        }
    }
    for (const ScenePrefabInstanceHandle handle : state.prefabInstances.HandlesSince(recording->firstNewPrefabInstanceId)) {
        if (!recording->prefabInstanceHandles.contains(handle)) {
            entry.prefabInstances.push_back(SceneHistoryPrefabInstanceChange{
                .handle = handle,
                .after = CaptureSceneHistoryPrefabInstance(state.prefabInstances.Find(handle)),
            });
        }
    }
}

// Brings every object and instance the entry changed to its state before (undo) or after (redo) the command.
// Objects it recreates get new entities, listed in `recreated`.
[[nodiscard]] bool Apply(Scene& scene, const SceneHistoryEntry& entry, bool undo, std::vector<SceneEntityRemap>& recreated) {
    SceneState& state = SceneAccess::State(scene);
    const auto target = [undo](const SceneHistoryObjectChange& change) -> const SceneHistoryObjectState& {
        return undo ? change.before : change.after;
    };
    std::unordered_map<EntityId, SceneEntity> replacements;
    const auto current = [&replacements](SceneEntity entity) {
        const auto replacement = replacements.find(entity.Id());
        return replacement == replacements.end() ? entity : replacement->second;
    };

    // Objects the target state has but the scene lost come back first, so every link below can name them.
    for (const SceneHistoryObjectChange& change : entry.objects) {
        const SceneHistoryObjectState& objectState = target(change);
        if (objectState.alive && !SceneEntityService::IsAlive(scene, change.entity)) {
            const SceneEntity entity = SceneEntityService::CreateEntity(scene, SceneObjectDesc{ .name = objectState.node.name });
            replacements.emplace(change.entity.Id(), entity);
            recreated.push_back(SceneEntityRemap{ .from = change.entity, .to = entity });
        }
    }

    // Reparent: objects whose parent differs are detached first and attached parents-first, so no step can form a
    // cycle while the other changed objects still sit where the command left them.
    std::vector<std::size_t> moving;
    std::unordered_map<EntityId, std::size_t> movingByEntity;
    for (std::size_t index = 0U; index < entry.objects.size(); ++index) {
        const SceneHistoryObjectChange& change = entry.objects[index];
        if (!target(change).alive) {
            continue;
        }
        const SceneEntity entity = current(change.entity);
        if (SceneHierarchyService::Parent(scene, entity) != current(target(change).parent)) {
            moving.push_back(index);
            movingByEntity.emplace(entity.Id(), index);
        }
    }
    std::vector<std::size_t> depth(entry.objects.size(), 0U);
    for (const std::size_t index : moving) {
        SceneEntity parent = current(target(entry.objects[index]).parent);
        for (auto ancestor = movingByEntity.find(parent.Id()); ancestor != movingByEntity.end() && depth[index] <= moving.size();
             ancestor = movingByEntity.find(parent.Id())) {
            ++depth[index];
            parent = current(target(entry.objects[ancestor->second]).parent);
        }
    }
    for (const std::size_t index : moving) {
        const SceneEntity entity = current(entry.objects[index].entity);
        if (SceneHierarchyService::Parent(scene, entity).IsValid()) {
            static_cast<void>(SceneHierarchyService::SetParent(scene, entity, SceneEntity{}));
        }
    }
    std::ranges::stable_sort(moving, {}, [&depth](std::size_t index) noexcept { return depth[index]; });
    for (const std::size_t index : moving) {
        const SceneEntity parent = current(target(entry.objects[index]).parent);
        if (parent.IsValid()) {
            static_cast<void>(SceneHierarchyService::SetParent(scene, current(entry.objects[index].entity), parent));
        }
    }

    // Objects the target state does not have. Their children in it were moved away above.
    for (const SceneHistoryObjectChange& change : entry.objects) {
        if (!target(change).alive && SceneEntityService::IsAlive(scene, change.entity)) {
            SceneEntityService::DestroyEntity(scene, change.entity);
        }
    }

    {
        ScenePrefabNodeStateWriterContext context{ scene, state };
        for (const SceneHistoryObjectChange& change : entry.objects) {
            const SceneHistoryObjectState& objectState = target(change);
            if (!objectState.alive) {
                continue;
            }
            const SceneEntity entity = current(change.entity);
            const SceneEntity parent = current(objectState.parent);
            ScenePrefabNodeDesc node = objectState.node;
            ScenePrefabReferenceResolver::EntityMap references;
            ForEachObjectReference(node.components, [&current, &references](std::uint64_t& reference) {
                if (reference != 0U) {
                    reference = current(SceneEntity{ reference }).Id();
                    references.emplace(reference, SceneEntity{ reference });
                }
            });
            ScenePrefabNodeStateWriter::Write(context, SceneAccess::MakeObject(scene, entity),
                parent.IsValid() ? SceneAccess::MakeObject(scene, parent) : SceneObject{}, node);
            if (!node.components.joint.has_value()) {
                scene.Components().Joints().Remove(entity);
            }
            ScenePrefabReferenceResolver::Apply(scene, node, entity, references);
            SceneEntityService::SetActive(scene, entity, objectState.active);
            SceneEntityService::SetPersistent(scene, entity, objectState.persistent);
            SceneEntityService::ReplaceBehaviourVariableOverrides(scene, entity, objectState.behaviourVariableOverrides);
        }
    }

    RelinkReferences(scene, replacements);

    // Sibling order: the objects with a recorded position are taken out and put back at it, lowest first. The
    // other siblings kept their relative order, so this rebuilds the recorded order.
    std::unordered_map<EntityId, std::pair<SceneEntity, std::vector<std::pair<std::uint32_t, SceneEntity>>>> placements;
    for (const SceneHistoryObjectChange& change : entry.objects) {
        const SceneHistoryObjectState& objectState = target(change);
        const std::uint32_t position = undo ? change.beforeSiblingIndex : change.afterSiblingIndex;
        if (!objectState.alive || position == kNoSiblingIndex) {
            continue;
        }
        const SceneEntity parent = current(objectState.parent);
        auto& placement = placements[SiblingsKey(parent)];
        placement.first = parent;
        placement.second.emplace_back(position, current(change.entity));
    }
    for (auto& [key, placement] : placements) {
        static_cast<void>(key);
        auto& [parent, placed] = placement;
        std::ranges::sort(placed, {}, [](const std::pair<std::uint32_t, SceneEntity>& item) noexcept { return item.first; });
        std::unordered_set<EntityId> placedIds;
        for (const auto& item : placed) {
            placedIds.insert(item.second.Id());
        }
        const std::vector<SceneEntity> siblings = Siblings(state, parent);
        std::vector<SceneEntity> ordered;
        ordered.reserve(siblings.size());
        for (const SceneEntity sibling : siblings) {
            if (!placedIds.contains(sibling.Id())) {
                ordered.push_back(sibling);
            }
        }
        for (const auto& [position, entity] : placed) {
            ordered.insert(ordered.begin() + static_cast<std::ptrdiff_t>(std::min<std::size_t>(position, ordered.size())), entity);
        }
        if (ordered != siblings) {
            SceneHierarchyCache::ReorderChildren(state, parent, ordered);
        }
    }

    bool restored = true;
    for (const SceneHistoryPrefabInstanceChange& change : entry.prefabInstances) {
        const SceneHistoryPrefabInstanceState& instance = undo ? change.before : change.after;
        static_cast<void>(state.prefabInstances.Remove(change.handle));
        if (!instance.present) {
            continue;
        }
        const auto object = [&scene, &current](SceneEntity entity) {
            const SceneEntity live = current(entity);
            return live.IsValid() && SceneEntityService::IsAlive(scene, live) ? SceneAccess::MakeObject(scene, live) : SceneObject{};
        };
        std::vector<SceneObject> objects;
        objects.reserve(instance.objects.size());
        for (const SceneEntity entity : instance.objects) {
            objects.push_back(object(entity));
        }
        restored = state.prefabInstances.Restore(
            change.handle,
            instance.prefab,
            instance.prefabGuid,
            object(instance.rootParent),
            std::move(objects),
            instance.resolvedPrefab == nullptr ? ScenePrefab{} : *instance.resolvedPrefab,
            std::span<const std::uint64_t>{ instance.nodeIds }) && restored;
    }

    // The writes above ran without prefab change tracking; let the instances see what changed.
    for (const SceneHistoryObjectChange& change : entry.objects) {
        if (target(change).alive) {
            MarkScenePrefabTopologyDirty(state, current(change.entity));
        }
    }

    return ApplySettings(scene, undo ? entry.settingsBefore : entry.settingsAfter) && restored;
}

void RemapRecordedEntities(SceneState& state, std::span<const SceneEntityRemap> remap) {
    if (remap.empty()) {
        return;
    }
    std::vector<SceneEntityRemap> sorted{ remap.begin(), remap.end() };
    std::ranges::sort(sorted, {}, [](const SceneEntityRemap& entry) noexcept {
        return entry.from.Id();
    });
    state.undoHistory.RemapEntities(sorted);
    state.redoHistory.RemapEntities(sorted);
    if (SceneHistoryRecording* recording = state.historyRecording.get(); recording != nullptr) {
        RemapSceneHistoryObjects(recording->objects, sorted);
        RemapSceneHistoryPrefabInstances(recording->prefabInstances, sorted);
        recording->objectIndex.clear();
        for (std::size_t index = 0U; index < recording->objects.size(); ++index) {
            recording->objectIndex.emplace(recording->objects[index].entity.Id(), index);
        }
        std::unordered_map<EntityId, SceneEntity> replacements;
        for (const SceneEntityRemap& entry : sorted) {
            replacements.emplace(entry.from.Id(), entry.to);
        }
        std::unordered_map<EntityId, std::vector<SceneEntity>> siblings;
        for (auto& [key, list] : recording->siblingsBefore) {
            const auto parent = key == 0U ? replacements.end() : replacements.find(key);
            for (SceneEntity& sibling : list) {
                if (const auto replacement = replacements.find(sibling.Id()); replacement != replacements.end()) {
                    sibling = replacement->second;
                }
            }
            siblings.emplace(parent == replacements.end() ? key : parent->second.Id(), std::move(list));
        }
        recording->siblingsBefore = std::move(siblings);
    }
}

} // namespace

bool SceneHistoryService::Record(Scene& scene, std::string label) {
    SceneState& state = SceneAccess::State(scene);
    Seal(scene, state);
    state.undoHistory.Push(SceneHistoryEntry{ .label = std::move(label), .settingsBefore = CaptureSettings(scene) });
    state.redoHistory.Clear();
    auto recording = std::make_unique<SceneHistoryRecording>();
    recording->firstNewPrefabInstanceId = state.prefabInstances.NextHandleId();
    state.prefabInstances.SetJournal(recording.get());
    state.historyRecording = std::move(recording);
    return true;
}

void SceneHistoryService::Commit(Scene& scene) {
    Seal(scene, SceneAccess::State(scene));
}

bool SceneHistoryService::CanUndo(const Scene& scene) noexcept {
    return !SceneAccess::State(scene).undoHistory.Empty();
}

bool SceneHistoryService::CanRedo(const Scene& scene) noexcept {
    return !SceneAccess::State(scene).redoHistory.Empty();
}

bool SceneHistoryService::Undo(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    Seal(scene, state);
    if (state.undoHistory.Empty()) {
        return false;
    }

    SceneHistoryEntry entry = state.undoHistory.Pop();
    std::vector<SceneEntityRemap> recreated;
    const bool restored = Apply(scene, entry, true, recreated);
    state.redoHistory.Push(std::move(entry));
    RemapRecordedEntities(state, recreated);
    state.recreatedEntities = std::move(recreated);
    return restored;
}

bool SceneHistoryService::Redo(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    Seal(scene, state);
    if (state.redoHistory.Empty()) {
        return false;
    }

    SceneHistoryEntry entry = state.redoHistory.Pop();
    std::vector<SceneEntityRemap> recreated;
    const bool restored = Apply(scene, entry, false, recreated);
    state.undoHistory.Push(std::move(entry));
    RemapRecordedEntities(state, recreated);
    state.recreatedEntities = std::move(recreated);
    return restored;
}

void SceneHistoryService::Clear(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.prefabInstances.SetJournal(nullptr);
    state.historyRecording.reset();
    state.undoHistory.Clear();
    state.redoHistory.Clear();
}

std::size_t SceneHistoryService::UndoCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).undoHistory.Size();
}

std::size_t SceneHistoryService::RedoCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).redoHistory.Size();
}

std::size_t SceneHistoryService::RecordedBytes(const Scene& scene) noexcept {
    const SceneState& state = SceneAccess::State(scene);
    return state.undoHistory.Bytes() + state.redoHistory.Bytes();
}

std::vector<SceneEntityRemap> SceneHistoryService::TakeRecreatedEntities(Scene& scene) noexcept {
    return std::exchange(SceneAccess::State(scene).recreatedEntities, {});
}

void SceneHistoryService::RemapEntities(Scene& scene, std::span<const SceneEntityRemap> remap) {
    RemapRecordedEntities(SceneAccess::State(scene), remap);
}

void SceneHistoryService::NoteObjectChanging(Scene& scene, SceneEntity entity) noexcept {
    SceneState& state = SceneAccess::State(scene);
    SceneHistoryRecording* recording = ActiveRecording(state);
    if (recording == nullptr || !entity.IsValid()) {
        return;
    }
    try {
        static_cast<void>(Track(scene, *recording, entity));
    } catch (...) {
        recording->failed = true;
    }
}

void SceneHistoryService::NoteObjectMoving(Scene& scene, SceneEntity entity, SceneEntity newParent) noexcept {
    SceneState& state = SceneAccess::State(scene);
    SceneHistoryRecording* recording = ActiveRecording(state);
    if (recording == nullptr || !entity.IsValid() || !SceneEntityService::IsAlive(scene, entity)) {
        return;
    }
    try {
        const std::size_t index = Track(scene, *recording, entity);
        recording->structural[index] = true;
        KeepSiblings(state, *recording, SceneHierarchyCache::Parent(state, entity));
        KeepSiblings(state, *recording, newParent);
    } catch (...) {
        recording->failed = true;
    }
}

void SceneHistoryService::NoteSubtreeDestroying(Scene& scene, SceneEntity root) noexcept {
    SceneState& state = SceneAccess::State(scene);
    SceneHistoryRecording* recording = ActiveRecording(state);
    if (recording == nullptr || !root.IsValid() || !SceneEntityService::IsAlive(scene, root)) {
        return;
    }
    try {
        std::vector<SceneEntity> pending{ root };
        while (!pending.empty()) {
            const SceneEntity entity = pending.back();
            pending.pop_back();
            const std::size_t index = Track(scene, *recording, entity);
            recording->structural[index] = true;
            KeepSiblings(state, *recording, SceneHierarchyCache::Parent(state, entity));
            const std::vector<SceneEntity> children = SceneHierarchyCache::Children(state, entity);
            pending.insert(pending.end(), children.begin(), children.end());
        }
    } catch (...) {
        recording->failed = true;
    }
}

void SceneHistoryService::NoteObjectsCreated(Scene& scene, std::span<const SceneEntity> entities) noexcept {
    SceneState& state = SceneAccess::State(scene);
    SceneHistoryRecording* recording = ActiveRecording(state);
    if (recording == nullptr) {
        return;
    }
    try {
        for (const SceneEntity entity : entities) {
            if (!recording->objectIndex.contains(entity.Id())) {
                const std::size_t index = recording->objects.size();
                recording->objects.push_back(SceneHistoryObjectChange{ .entity = entity });
                recording->structural.push_back(true);
                recording->objectIndex.emplace(entity.Id(), index);
            }
        }
    } catch (...) {
        recording->failed = true;
    }
}

} // namespace kb::scene

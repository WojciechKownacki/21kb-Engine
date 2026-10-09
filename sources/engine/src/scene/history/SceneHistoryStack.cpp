#include "scene/history/SceneHistoryStack.hpp"

#include "engine/ui/UIEntityReferences.hpp"

#include <algorithm>
#include <new>
#include <utility>

namespace kb::scene {
namespace {

[[nodiscard]] const SceneEntityRemap* FindRemap(std::span<const SceneEntityRemap> remap, SceneEntity::IdType id) noexcept {
    const auto replacement = std::ranges::lower_bound(remap, id, {}, [](const SceneEntityRemap& candidate) noexcept {
        return candidate.from.Id();
    });
    return replacement != remap.end() && replacement->from.Id() == id ? &*replacement : nullptr;
}

void Remap(SceneEntity& entity, std::span<const SceneEntityRemap> remap) noexcept {
    if (const SceneEntityRemap* replacement = FindRemap(remap, entity.Id()); replacement != nullptr) {
        entity = replacement->to;
    }
}

void RemapId(std::uint64_t& id, std::span<const SceneEntityRemap> remap) noexcept {
    if (const SceneEntityRemap* replacement = id == 0U ? nullptr : FindRemap(remap, id); replacement != nullptr) {
        id = replacement->to.Id();
    }
}

void RemapState(SceneHistoryObjectState& state, std::span<const SceneEntityRemap> remap) noexcept {
    Remap(state.parent, remap);
    ScenePrefabNodeComponents& components = state.node.components;
    if (components.joint.has_value()) {
        RemapId(components.joint->connectedNodeStableId, remap);
    }
    if (components.regionPortal.has_value()) {
        RemapId(components.regionPortal->sourceCellNodeStableId, remap);
        RemapId(components.regionPortal->targetCellNodeStableId, remap);
    }
    if (components.lensEcho.has_value()) {
        RemapId(components.lensEcho->sourceNodeStableId, remap);
    }
    ForEachUIEntityReference(components.ui, [remap](std::uint64_t& reference) noexcept {
        RemapId(reference, remap);
    });
}

void RemapInstance(SceneHistoryPrefabInstanceState& state, std::span<const SceneEntityRemap> remap) noexcept {
    Remap(state.rootParent, remap);
    for (SceneEntity& object : state.objects) {
        Remap(object, remap);
    }
}

[[nodiscard]] std::size_t StateBytes(const SceneHistoryObjectState& state) noexcept {
    std::size_t bytes = state.node.name.capacity() + state.node.nestedPrefabGuid.capacity();
    bytes += state.behaviourVariableOverrides.capacity() * sizeof(BehaviourVariableOverride);
    for (const BehaviourVariableOverride& variable : state.behaviourVariableOverrides) {
        bytes += variable.name.capacity();
    }
    return bytes;
}

[[nodiscard]] std::size_t InstanceBytes(const SceneHistoryPrefabInstanceState& state) noexcept {
    std::size_t bytes = state.prefabGuid.capacity() + state.objects.capacity() * sizeof(SceneEntity) +
        state.nodeIds.capacity() * sizeof(std::uint64_t);
    if (state.resolvedPrefab != nullptr) {
        bytes += sizeof(ScenePrefab) + state.resolvedPrefab->NodeCount() * sizeof(ScenePrefabNodeDesc);
    }
    return bytes;
}

} // namespace

void SceneHistoryRecording::BeforeChange(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord* record) noexcept {
    if (busy || failed || !handle.IsValid()) {
        return;
    }
    try {
        if (!prefabInstanceHandles.insert(handle).second) {
            return;
        }
        prefabInstances.push_back(SceneHistoryPrefabInstanceChange{ .handle = handle, .before = CaptureSceneHistoryPrefabInstance(record) });
    } catch (...) {
        failed = true;
    }
}

SceneHistoryPrefabInstanceState CaptureSceneHistoryPrefabInstance(const ScenePrefabInstanceRecord* record) {
    SceneHistoryPrefabInstanceState state;
    if (record == nullptr) {
        return state;
    }
    state.present = true;
    state.prefab = record->prefab;
    state.prefabGuid = std::string{ record->PrefabGuid() };
    state.rootParent = record->rootParent.Entity();
    const std::span<const SceneObject> objects = record->Objects();
    state.objects.reserve(objects.size());
    for (const SceneObject object : objects) {
        state.objects.push_back(object.Entity());
    }
    const std::span<const std::uint64_t> nodeIds = record->NodeIds();
    state.nodeIds.assign(nodeIds.begin(), nodeIds.end());
    if (record->sharedResolvedPrefab != nullptr && record->resolvedPrefab.Empty() && record->pooledResolvedPrefab == nullptr) {
        state.resolvedPrefab = record->sharedResolvedPrefab;
    } else if (const ScenePrefab* resolved = record->ResolvedPrefab(); resolved != nullptr) {
        state.resolvedPrefab = std::make_shared<const ScenePrefab>(*resolved);
    }
    return state;
}

bool SceneHistoryStack::Empty() const noexcept {
    return entries_.empty();
}

std::size_t SceneHistoryStack::Size() const noexcept {
    return entries_.size();
}

void SceneHistoryStack::Push(SceneHistoryEntry entry) {
    entries_.push_back(std::move(entry));
}

SceneHistoryEntry SceneHistoryStack::Pop() {
    SceneHistoryEntry entry = std::move(entries_.back());
    entries_.pop_back();
    return entry;
}

SceneHistoryEntry& SceneHistoryStack::Top() noexcept {
    return entries_.back();
}

void SceneHistoryStack::Clear() noexcept {
    entries_.clear();
}

void SceneHistoryStack::RemapEntities(std::span<const SceneEntityRemap> remap) noexcept {
    for (SceneHistoryEntry& entry : entries_) {
        RemapSceneHistoryObjects(entry.objects, remap);
        RemapSceneHistoryPrefabInstances(entry.prefabInstances, remap);
    }
}

std::size_t SceneHistoryStack::Bytes() const noexcept {
    std::size_t bytes = entries_.capacity() * sizeof(SceneHistoryEntry);
    for (const SceneHistoryEntry& entry : entries_) {
        bytes += SceneHistoryEntryBytes(entry);
    }
    return bytes;
}

void RemapSceneHistoryObjects(std::span<SceneHistoryObjectChange> objects, std::span<const SceneEntityRemap> remap) noexcept {
    if (remap.empty()) {
        return;
    }
    for (SceneHistoryObjectChange& change : objects) {
        Remap(change.entity, remap);
        RemapState(change.before, remap);
        RemapState(change.after, remap);
    }
}

void RemapSceneHistoryPrefabInstances(std::span<SceneHistoryPrefabInstanceChange> instances, std::span<const SceneEntityRemap> remap) noexcept {
    if (remap.empty()) {
        return;
    }
    for (SceneHistoryPrefabInstanceChange& change : instances) {
        RemapInstance(change.before, remap);
        RemapInstance(change.after, remap);
    }
}

std::size_t SceneHistoryEntryBytes(const SceneHistoryEntry& entry) noexcept {
    std::size_t bytes = entry.label.capacity() + entry.settingsBefore.audioMixerSnapshot.capacity() + entry.settingsAfter.audioMixerSnapshot.capacity();
    bytes += entry.objects.capacity() * sizeof(SceneHistoryObjectChange);
    for (const SceneHistoryObjectChange& change : entry.objects) {
        bytes += StateBytes(change.before) + StateBytes(change.after);
    }
    bytes += entry.prefabInstances.capacity() * sizeof(SceneHistoryPrefabInstanceChange);
    for (const SceneHistoryPrefabInstanceChange& change : entry.prefabInstances) {
        bytes += InstanceBytes(change.before) + InstanceBytes(change.after);
    }
    return bytes;
}

} // namespace kb::scene

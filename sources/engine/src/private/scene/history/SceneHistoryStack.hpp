#pragma once

#include "engine/scene/BehaviourVariableOverride.hpp"
#include "engine/scene/SceneAudioOcclusionAccess.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneHistory.hpp"
#include "engine/scene/ScenePrefab.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabInstanceHandle.hpp"
#include "engine/scene/ScenePrefabNode.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kb::scene {

// What history restores of one object. Entity references inside the components (joints, portals, lens echoes,
// UI links) stay entity ids, remapped when history recreates the objects they name.
struct SceneHistoryObjectState {
    bool alive = false;
    SceneEntity parent{};
    // Name, local transform, visibility and components.
    ScenePrefabNodeDesc node;
    bool active = true;
    bool persistent = false;
    std::vector<BehaviourVariableOverride> behaviourVariableOverrides;
};

// One object a command changed, with its state before and after the command.
struct SceneHistoryObjectChange {
    static constexpr std::uint32_t NoSiblingIndex = UINT32_MAX;

    SceneEntity entity{};
    SceneHistoryObjectState before;
    SceneHistoryObjectState after;
    // Position among its siblings, kept when the command created, destroyed or reparented the object.
    std::uint32_t beforeSiblingIndex = NoSiblingIndex;
    std::uint32_t afterSiblingIndex = NoSiblingIndex;
};

struct SceneHistoryPrefabInstanceState {
    bool present = false;
    ScenePrefabHandle prefab;
    std::string prefabGuid;
    SceneEntity rootParent{};
    std::vector<SceneEntity> objects;
    std::vector<std::uint64_t> nodeIds;
    std::shared_ptr<const ScenePrefab> resolvedPrefab;
};

struct SceneHistoryPrefabInstanceChange {
    ScenePrefabInstanceHandle handle;
    SceneHistoryPrefabInstanceState before;
    SceneHistoryPrefabInstanceState after;
};

struct SceneHistorySettings {
    std::uint64_t audioMixerAssetId = 0U;
    std::string audioMixerSnapshot;
    AudioOcclusionSettings audioOcclusionSettings;
};

// One recorded command: only the objects and prefab instances it changed.
struct SceneHistoryEntry {
    std::string label;
    SceneHistorySettings settingsBefore;
    SceneHistorySettings settingsAfter;
    std::vector<SceneHistoryObjectChange> objects;
    std::vector<SceneHistoryPrefabInstanceChange> prefabInstances;
};

// The command Record opened. Every write through the scene API first reports the object it is about to change,
// which keeps that object's state from before the command; the registry reports instance records the same way.
struct SceneHistoryRecording final : ScenePrefabInstanceJournal {
    void BeforeChange(ScenePrefabInstanceHandle handle, const ScenePrefabInstanceRecord* record) noexcept override;

    std::unordered_map<SceneEntity::IdType, std::size_t> objectIndex;
    std::vector<SceneHistoryObjectChange> objects;
    // Parallel to objects: the command created, destroyed or reparented the object.
    std::vector<bool> structural;
    // Child lists as they were before the command first changed them, keyed by parent id (0: the roots).
    std::unordered_map<SceneEntity::IdType, std::vector<SceneEntity>> siblingsBefore;
    std::vector<SceneHistoryPrefabInstanceChange> prefabInstances;
    std::set<ScenePrefabInstanceHandle> prefabInstanceHandles;
    std::uint64_t firstNewPrefabInstanceId = 0U;
    // Writes are recorded on the thread that opened the command; scene work running on other threads (systems
    // stepping the scene in parallel) is not part of an edit.
    std::thread::id thread = std::this_thread::get_id();
    // Set while history itself reads or writes the scene, so its own access is not reported back.
    bool busy = false;
    // A state could not be kept (out of memory): the command cannot be undone exactly.
    bool failed = false;
};

[[nodiscard]] SceneHistoryPrefabInstanceState CaptureSceneHistoryPrefabInstance(const ScenePrefabInstanceRecord* record);

class SceneHistoryStack {
public:
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Size() const noexcept;
    void Push(SceneHistoryEntry entry);
    [[nodiscard]] SceneHistoryEntry Pop();
    [[nodiscard]] SceneHistoryEntry& Top() noexcept;
    void Clear() noexcept;
    // `remap` is sorted by the id of the entity it replaces.
    void RemapEntities(std::span<const SceneEntityRemap> remap) noexcept;
    [[nodiscard]] std::size_t Bytes() const noexcept;

private:
    std::vector<SceneHistoryEntry> entries_;
};

// Rewrites every entity a change names (objects, parents, component references, instance objects).
void RemapSceneHistoryObjects(std::span<SceneHistoryObjectChange> objects, std::span<const SceneEntityRemap> remap) noexcept;
void RemapSceneHistoryPrefabInstances(std::span<SceneHistoryPrefabInstanceChange> instances, std::span<const SceneEntityRemap> remap) noexcept;
[[nodiscard]] std::size_t SceneHistoryEntryBytes(const SceneHistoryEntry& entry) noexcept;

} // namespace kb::scene

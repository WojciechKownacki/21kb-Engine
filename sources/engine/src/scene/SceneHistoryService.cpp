#include "scene/SceneHistoryService.hpp"

#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/ScenePrefabInstance.hpp"
#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/ScenePrefabInstanceRegistry.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] std::vector<SceneObject> CopyObjects(const ScenePrefabInstance& instance) {
    return { instance.Objects().begin(), instance.Objects().end() };
}

[[nodiscard]] bool Capture(Scene& scene, std::string label, SceneHistoryEntry& output) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<ScenePrefab> roots;
    std::vector<SceneHistoryPrefabInstanceSnapshot> prefabInstances;
    for (const SceneEntity root : SceneHierarchyService::RootEntities(scene)) {
        const SceneObject rootObject = SceneAccess::MakeObject(scene, root);
        const ScenePrefabInstanceHandle instanceHandle = state.prefabInstances.FindRootInstance(rootObject);
        if (instanceHandle.IsValid()) {
            const ScenePrefabInstanceRecord* record = state.prefabInstances.Find(instanceHandle);
            if (record == nullptr) {
                return false;
            }

            prefabInstances.push_back(SceneHistoryPrefabInstanceSnapshot{
                .handle = instanceHandle,
                .prefab = record->prefab,
                .prefabGuid = record->prefabGuid,
                .rootParent = record->rootParent,
                .resolvedPrefab = record->resolvedPrefab,
                .currentState = ScenePrefabCaptureService::Capture(scene, rootObject, ScenePrefabCaptureSettings{}),
            });
            continue;
        }

        roots.push_back(ScenePrefabCaptureService::Capture(scene, rootObject, ScenePrefabCaptureSettings{}));
    }
    output = SceneHistoryEntry{
        .label = std::move(label),
        .roots = std::move(roots),
        .prefabInstances = std::move(prefabInstances),
    };
    return true;
}

[[nodiscard]] bool RestoreSnapshot(Scene& scene, const SceneHistoryEntry& entry) {
    SceneState& state = SceneAccess::State(scene);
    const std::vector<SceneEntity> roots = SceneHierarchyService::RootEntities(scene);
    for (const SceneEntity root : roots) {
        SceneEntityService::DestroyEntity(scene, root);
    }
    state.prefabInstances.Clear();

    for (const ScenePrefab& prefab : entry.roots) {
        if (ScenePrefabInstantiationService::Instantiate(scene, prefab, ScenePrefabInstantiationSettings{}).Empty()) {
            return false;
        }
    }

    for (const SceneHistoryPrefabInstanceSnapshot& snapshot : entry.prefabInstances) {
        ScenePrefabInstantiationSettings settings;
        if (snapshot.rootParent.IsValid() && scene.Entities().IsAlive(snapshot.rootParent)) {
            settings.parent = snapshot.rootParent;
        }

        const ScenePrefabInstance instance = ScenePrefabInstantiationService::Instantiate(scene, snapshot.currentState, settings);
        if (instance.Empty()) {
            return false;
        }

        if (!state.prefabInstances.Restore(
                snapshot.handle,
                snapshot.prefab,
                snapshot.prefabGuid,
                settings.parent,
                CopyObjects(instance),
                snapshot.resolvedPrefab)) {
            return false;
        }
    }
    return true;
}

} // namespace

bool SceneHistoryService::Record(Scene& scene, std::string label) {
    SceneState& state = SceneAccess::State(scene);
    SceneHistoryEntry entry;
    if (!Capture(scene, std::move(label), entry)) {
        return false;
    }
    state.undoHistory.Push(std::move(entry));
    state.redoHistory.Clear();
    return true;
}

bool SceneHistoryService::CanUndo(const Scene& scene) noexcept {
    return !SceneAccess::State(scene).undoHistory.Empty();
}

bool SceneHistoryService::CanRedo(const Scene& scene) noexcept {
    return !SceneAccess::State(scene).redoHistory.Empty();
}

bool SceneHistoryService::Undo(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (state.undoHistory.Empty()) {
        return false;
    }

    SceneHistoryEntry current;
    if (!Capture(scene, {}, current)) {
        return false;
    }
    SceneHistoryEntry previous = state.undoHistory.Pop();
    if (!RestoreSnapshot(scene, previous)) {
        return false;
    }
    state.redoHistory.Push(std::move(current));
    return true;
}

bool SceneHistoryService::Redo(Scene& scene) {
    SceneState& state = SceneAccess::State(scene);
    if (state.redoHistory.Empty()) {
        return false;
    }

    SceneHistoryEntry current;
    if (!Capture(scene, {}, current)) {
        return false;
    }
    SceneHistoryEntry next = state.redoHistory.Pop();
    if (!RestoreSnapshot(scene, next)) {
        return false;
    }
    state.undoHistory.Push(std::move(current));
    return true;
}

void SceneHistoryService::Clear(Scene& scene) noexcept {
    SceneState& state = SceneAccess::State(scene);
    state.undoHistory.Clear();
    state.redoHistory.Clear();
}

std::size_t SceneHistoryService::UndoCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).undoHistory.Size();
}

std::size_t SceneHistoryService::RedoCount(const Scene& scene) noexcept {
    return SceneAccess::State(scene).redoHistory.Size();
}

} // namespace kb::scene

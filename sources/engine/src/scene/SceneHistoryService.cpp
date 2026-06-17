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

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kb::scene {
namespace {

using SceneHistoryObjectPathIndex = std::unordered_map<std::uint64_t, SceneHistoryObjectPath>;

void IndexObjectPath(SceneObject object, SceneHistoryObjectPath path, SceneHistoryObjectPathIndex& output) {
    output[object.Entity().Id()] = path;

    const std::vector<SceneObject> children = object.Children();
    for (std::uint32_t childIndex = 0U; childIndex < static_cast<std::uint32_t>(children.size()); ++childIndex) {
        SceneHistoryObjectPath childPath = path;
        childPath.push_back(childIndex);
        IndexObjectPath(children[childIndex], std::move(childPath), output);
    }
}

[[nodiscard]] SceneHistoryObjectPathIndex BuildObjectPathIndex(const std::vector<SceneObject>& roots) {
    SceneHistoryObjectPathIndex index;
    for (std::uint32_t rootIndex = 0U; rootIndex < static_cast<std::uint32_t>(roots.size()); ++rootIndex) {
        IndexObjectPath(roots[rootIndex], SceneHistoryObjectPath{ rootIndex }, index);
    }
    return index;
}

[[nodiscard]] SceneHistoryObjectPath FindObjectPath(Scene& scene, const SceneHistoryObjectPathIndex& index, SceneObject object) {
    if (!object.IsValid() || !scene.Entities().IsAlive(object)) {
        return {};
    }

    const auto iterator = index.find(object.Entity().Id());
    return iterator == index.end() ? SceneHistoryObjectPath{} : iterator->second;
}

[[nodiscard]] SceneObject ResolveObjectPath(Scene& scene, const std::vector<SceneObject>& roots, const SceneHistoryObjectPath& path) {
    if (path.empty() || path.front() >= roots.size()) {
        return {};
    }

    SceneObject object = roots[path.front()];
    for (std::size_t depth = 1U; depth < path.size(); ++depth) {
        const std::vector<SceneObject> children = SceneHierarchyService::Children(scene, object);
        if (path[depth] >= children.size()) {
            return {};
        }
        object = children[path[depth]];
    }
    return object;
}

[[nodiscard]] bool Capture(Scene& scene, std::string label, SceneHistoryEntry& output) {
    SceneState& state = SceneAccess::State(scene);
    std::vector<ScenePrefab> roots;
    std::vector<SceneHistoryPrefabInstanceSnapshot> prefabInstances;
    std::vector<SceneObject> rootObjects;
    for (const SceneEntity root : SceneHierarchyService::RootEntities(scene)) {
        const SceneObject rootObject = SceneAccess::MakeObject(scene, root);
        rootObjects.push_back(rootObject);
        roots.push_back(ScenePrefabCaptureService::Capture(scene, rootObject, ScenePrefabCaptureSettings{}));
    }

    const SceneHistoryObjectPathIndex objectPaths = BuildObjectPathIndex(rootObjects);
    for (const ScenePrefabInstanceHandle instanceHandle : state.prefabInstances.Handles()) {
        const ScenePrefabInstanceRecord* record = state.prefabInstances.Find(instanceHandle);
        if (record == nullptr) {
            return false;
        }

        SceneHistoryPrefabInstanceSnapshot snapshot{
            .handle = instanceHandle,
            .prefab = record->prefab,
            .prefabGuid = record->prefabGuid,
            .rootParentPath = FindObjectPath(scene, objectPaths, record->rootParent),
            .resolvedPrefab = record->resolvedPrefab,
        };
        snapshot.objectPaths.reserve(record->objects.size());
        for (const SceneObject object : record->objects) {
            snapshot.objectPaths.push_back(FindObjectPath(scene, objectPaths, object));
        }
        prefabInstances.push_back(std::move(snapshot));
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

    std::vector<SceneObject> restoredRoots;
    restoredRoots.reserve(entry.roots.size());
    for (const ScenePrefab& prefab : entry.roots) {
        ScenePrefabInstance root = ScenePrefabInstantiationService::Instantiate(scene, prefab, ScenePrefabInstantiationSettings{});
        if (root.Empty()) {
            return false;
        }
        restoredRoots.push_back(root.RootObject());
    }

    for (const SceneHistoryPrefabInstanceSnapshot& snapshot : entry.prefabInstances) {
        std::vector<SceneObject> objects;
        objects.reserve(snapshot.objectPaths.size());
        for (const SceneHistoryObjectPath& path : snapshot.objectPaths) {
            objects.push_back(ResolveObjectPath(scene, restoredRoots, path));
        }
        if (objects.empty()) {
            return false;
        }

        const SceneObject rootParent = ResolveObjectPath(scene, restoredRoots, snapshot.rootParentPath);
        if (!state.prefabInstances.Restore(
                snapshot.handle,
                snapshot.prefab,
                snapshot.prefabGuid,
                rootParent,
                std::move(objects),
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

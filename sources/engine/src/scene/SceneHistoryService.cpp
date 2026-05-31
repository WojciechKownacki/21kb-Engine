#include "scene/SceneHistoryService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneEntityService.hpp"
#include "scene/SceneHierarchyService.hpp"
#include "scene/SceneState.hpp"
#include "scene/prefab/ScenePrefabCaptureService.hpp"
#include "scene/prefab/ScenePrefabInstantiationService.hpp"

#include <utility>
#include <vector>

namespace kb::scene {
namespace {

[[nodiscard]] bool Capture(Scene& scene, std::string label, SceneHistoryEntry& output) {
    std::vector<ScenePrefab> roots;
    for (const SceneEntity root : SceneHierarchyService::RootEntities(scene)) {
        roots.push_back(ScenePrefabCaptureService::Capture(scene, SceneAccess::MakeObject(scene, root), ScenePrefabCaptureSettings{}));
    }
    output = SceneHistoryEntry{
        .label = std::move(label),
        .roots = std::move(roots),
    };
    return true;
}

void RestoreSnapshot(Scene& scene, const SceneHistoryEntry& entry) {
    const std::vector<SceneEntity> roots = SceneHierarchyService::RootEntities(scene);
    for (const SceneEntity root : roots) {
        SceneEntityService::DestroyEntity(scene, root);
    }
    for (const ScenePrefab& prefab : entry.roots) {
        static_cast<void>(ScenePrefabInstantiationService::Instantiate(scene, prefab, ScenePrefabInstantiationSettings{}));
    }
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
    RestoreSnapshot(scene, previous);
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
    RestoreSnapshot(scene, next);
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

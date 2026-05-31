#include "engine/scene/SceneHistory.hpp"

#include "scene/SceneHistoryService.hpp"

#include <utility>

namespace kb::scene {

SceneHistory::SceneHistory(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneHistory::Record(std::string label) {
    return SceneHistoryService::Record(scene_, std::move(label));
}

bool SceneHistory::CanUndo() const noexcept {
    return SceneHistoryService::CanUndo(scene_);
}

bool SceneHistory::CanRedo() const noexcept {
    return SceneHistoryService::CanRedo(scene_);
}

bool SceneHistory::Undo() {
    return SceneHistoryService::Undo(scene_);
}

bool SceneHistory::Redo() {
    return SceneHistoryService::Redo(scene_);
}

void SceneHistory::Clear() noexcept {
    SceneHistoryService::Clear(scene_);
}

std::size_t SceneHistory::UndoCount() const noexcept {
    return SceneHistoryService::UndoCount(scene_);
}

std::size_t SceneHistory::RedoCount() const noexcept {
    return SceneHistoryService::RedoCount(scene_);
}

} // namespace kb::scene

#include "engine/scene/SceneHistory.hpp"

#include "scene/SceneHistoryService.hpp"

#include <utility>

namespace kb::scene {

SceneHistory::SceneHistory(Scene& scene) noexcept
    : scene_(scene) {}

bool SceneHistory::Record(std::string label) {
    return SceneHistoryService::Record(scene_, std::move(label));
}

void SceneHistory::Commit() {
    SceneHistoryService::Commit(scene_);
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

std::size_t SceneHistory::RecordedBytes() const noexcept {
    return SceneHistoryService::RecordedBytes(scene_);
}

std::vector<SceneEntityRemap> SceneHistory::TakeRecreatedEntities() {
    return SceneHistoryService::TakeRecreatedEntities(scene_);
}

void SceneHistory::RemapEntities(std::span<const SceneEntityRemap> remap) {
    SceneHistoryService::RemapEntities(scene_, remap);
}

} // namespace kb::scene

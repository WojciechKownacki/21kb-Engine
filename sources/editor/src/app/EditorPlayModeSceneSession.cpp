#include "app/EditorPlayModeSceneSession.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"

#include <utility>

namespace kb::editor {

bool EditorPlayModeSceneSession::Active() const noexcept {
    return snapshot_.has_value();
}

bool EditorPlayModeSceneSession::Begin(kb::scene::Scene& scene, std::string sceneName) {
    if (Active()) {
        return true;
    }

    snapshot_ = kb::scene::SceneDocumentService::Capture(scene, std::move(sceneName));
    return snapshot_.has_value();
}

bool EditorPlayModeSceneSession::Restore(kb::scene::Scene& scene) {
    if (!Active()) {
        return true;
    }

    const bool restored = kb::scene::SceneDocumentService::LoadIntoScene(scene, *snapshot_);
    if (restored) {
        Clear();
    }
    return restored;
}

void EditorPlayModeSceneSession::Clear() noexcept {
    snapshot_.reset();
}

} // namespace kb::editor

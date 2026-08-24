#include "app/EditorPlayModeSceneSession.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneRuntime.hpp"

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
    if (!snapshot_.has_value()) {
        return false;
    }
    scene.Runtime().SetPlaying(true);
    return true;
}

bool EditorPlayModeSceneSession::Restore(kb::scene::Scene& scene) {
    if (!Active()) {
        return true;
    }

    scene.Runtime().SetPlaying(false);
    const bool restored = kb::scene::SceneDocumentService::LoadIntoScene(scene, *snapshot_);
    // Runtime mode is session state, not document state. Keep it stopped even if
    // loading fails or a future document version starts carrying runtime fields.
    scene.Runtime().SetPlaying(false);
    Clear();
    return restored;
}

void EditorPlayModeSceneSession::Clear() noexcept {
    snapshot_.reset();
}

} // namespace kb::editor

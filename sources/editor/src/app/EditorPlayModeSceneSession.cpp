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

    scene.History().Commit();
    snapshot_ = kb::scene::SceneDocumentService::Capture(scene, std::move(sceneName));
    if (!snapshot_.has_value()) {
        return false;
    }
    hierarchy_.CaptureAuthoredHierarchy(scene);
    history_ = scene.History().Suspend();
    owner_ = &scene;
    restoredEntities_.clear();
    scene.Runtime().SetPlaying(true);
    return true;
}

bool EditorPlayModeSceneSession::Restore(kb::scene::Scene& scene) {
    if (!Active()) {
        return true;
    }
    if (owner_ != &scene) return false;

    scene.Runtime().SetPlaying(false);
    const bool restored = kb::scene::SceneDocumentService::LoadIntoScene(scene, *snapshot_);
    // Runtime mode is session state, not document state. Keep it stopped even if
    // loading fails or a future document version starts carrying runtime fields.
    scene.Runtime().SetPlaying(false);
    if (!restored) return false;
    restoredEntities_ = hierarchy_.RecreatedEntities(scene);
    if (!scene.History().Resume(history_, restoredEntities_)) return false;
    snapshot_.reset();
    owner_ = nullptr;
    hierarchy_.Clear();
    return true;
}

std::span<const kb::scene::SceneEntityRemap> EditorPlayModeSceneSession::RestoredEntities() const noexcept {
    return restoredEntities_;
}

void EditorPlayModeSceneSession::Clear() noexcept {
    snapshot_.reset();
    history_ = {};
    owner_ = nullptr;
    hierarchy_.Clear();
    restoredEntities_.clear();
}

} // namespace kb::editor

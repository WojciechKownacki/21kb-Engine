#include "scene/systems/UIDocumentSceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"
#include "scene/SceneUIDocumentService.hpp"

namespace kb::scene {

void UIDocumentSceneSystem::OnUpdate(SceneSystemContext& context) {
    SceneUIDocumentService::SyncComponents(context.GetScene());
}

} // namespace kb::scene

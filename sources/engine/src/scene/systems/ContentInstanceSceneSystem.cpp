#include "scene/systems/ContentInstanceSceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"
#include "scene/SceneContentInstanceService.hpp"

namespace kb::scene {

void ContentInstanceSceneSystem::OnUpdate(SceneSystemContext& context) { SceneContentInstanceService::Synchronize(context.GetScene()); }
void ContentInstanceSceneSystem::OnDestroy(SceneSystemContext& context) { SceneContentInstanceService::Shutdown(context.GetScene()); }

} // namespace kb::scene

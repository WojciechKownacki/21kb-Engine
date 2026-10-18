#include "scene/systems/GameplaySceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"
#include "scene/SceneGameplayService.hpp"

namespace kb::scene {

void GameplaySceneSystem::OnUpdate(SceneSystemContext& context) {
    SceneGameplayService::Advance(context.GetScene(), context.DeltaSeconds());
}

} // namespace kb::scene

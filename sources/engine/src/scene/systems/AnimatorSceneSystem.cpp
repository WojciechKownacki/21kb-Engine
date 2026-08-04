#include "scene/systems/AnimatorSceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"
#include "scene/SceneAnimatorService.hpp"

namespace kb::scene {

void AnimatorSceneSystem::OnUpdate(SceneSystemContext& context) {
    SceneAnimatorService::SyncComponents(context.GetScene());
    SceneAnimatorService::Advance(context.GetScene(), context.DeltaSeconds());
    SceneAnimatorService::PublishDebugSnapshot(context.GetScene());
}

} // namespace kb::scene

#include "scene/systems/TimelineSceneSystem.hpp"

#include "engine/scene/SceneSystemContext.hpp"
#include "scene/SceneTimelineService.hpp"

namespace kb::scene {

void TimelineSceneSystem::OnUpdate(SceneSystemContext& context) {
    SceneTimelineService::Advance(
        context.GetScene(), context.DeltaSeconds());
}

} // namespace kb::scene

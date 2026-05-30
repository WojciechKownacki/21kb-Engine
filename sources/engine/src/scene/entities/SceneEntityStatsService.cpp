#include "scene/entities/SceneEntityStatsService.hpp"

#include "scene/SceneAccess.hpp"
#include "scene/SceneState.hpp"
#include "scene/entities/SceneEntityCounter.hpp"

namespace kb::scene {

std::size_t SceneEntityStatsService::Count(const Scene& scene) {
    const SceneState& state = SceneAccess::State(scene);
    return SceneEntityCounter::CountWithComponent(state.world, state.components.TransformComponentId());
}

} // namespace kb::scene

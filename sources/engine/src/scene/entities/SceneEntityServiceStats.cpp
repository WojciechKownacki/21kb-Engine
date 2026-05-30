#include "scene/SceneEntityService.hpp"

#include "scene/entities/SceneEntityStatsService.hpp"

namespace kb::scene {

std::size_t SceneEntityService::Count(const Scene& scene) {
    return SceneEntityStatsService::Count(scene);
}

} // namespace kb::scene

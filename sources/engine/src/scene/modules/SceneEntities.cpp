#include "engine/scene/SceneEntities.hpp"

namespace kb::scene {

SceneEntityQueries::SceneEntityQueries(const Scene& scene) noexcept
    : scene_(scene) {}

SceneEntities::SceneEntities(Scene& scene) noexcept
    : scene_(scene) {}

} // namespace kb::scene

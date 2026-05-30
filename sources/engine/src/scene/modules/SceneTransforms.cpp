#include "engine/scene/SceneTransforms.hpp"

namespace kb::scene {

SceneTransformQueries::SceneTransformQueries(const Scene& scene) noexcept
    : scene_(scene) {}

SceneTransforms::SceneTransforms(Scene& scene) noexcept
    : scene_(scene) {}

} // namespace kb::scene

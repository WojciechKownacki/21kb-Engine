#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace kb::scene {

class Scene;

// One enabled Planar Reflection in world space: the mirror's rectangle and how it is drawn.
struct ScenePlanarReflectionPlane {
    SceneEntity entity{};
    kb::math::DVec3 center{};
    // Unit axes of the rectangle in world space; `normal` points to the side that is reflected.
    std::array<float, 3> right{ 1.0F, 0.0F, 0.0F };
    std::array<float, 3> normal{ 0.0F, 1.0F, 0.0F };
    std::array<float, 3> forward{ 0.0F, 0.0F, 1.0F };
    float halfWidth = 0.0F;
    float halfDepth = 0.0F;
    float resolutionScale = 0.5F;
    std::uint32_t reflectedLayers = 0xFFFFFFFFU;
    float maxDistance = 100.0F;
    bool reflectShadows = false;
};

// Whether the scene has any Planar Reflection; a renderer does no mirror work for scenes without one.
[[nodiscard]] bool SceneHasPlanarReflections(const Scene& scene) noexcept;

// Appends the enabled, valid Planar Reflections of the scene (in entity order) to `out`.
void ScenePlanarReflectionPlanes(const Scene& scene, std::vector<ScenePlanarReflectionPlane>& out);

} // namespace kb::scene

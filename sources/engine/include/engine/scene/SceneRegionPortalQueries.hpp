#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/RegionPortalComponent.hpp"

namespace kb::scene {

class Scene;

[[nodiscard]] bool SceneRegionPortalContains(const Scene& scene, SceneEntity portal, kb::math::Vec3 worldPoint) noexcept;
[[nodiscard]] bool SceneRegionPortalAllows(const Scene& scene, SceneEntity portal, SceneEntity sourceCell, SceneEntity targetCell, RegionPortalPurpose purpose) noexcept;

} // namespace kb::scene

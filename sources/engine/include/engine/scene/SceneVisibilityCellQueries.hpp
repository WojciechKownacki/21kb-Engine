#pragma once

#include "engine/math/EngineMath.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

namespace kb::scene {

class Scene;

// Resolves only authored ECS data. A portal graph is intentionally absent
// here: it is derived from RegionPortal components by its consumer.
[[nodiscard]] bool SceneVisibilityCellContains(const Scene& scene, SceneEntity entity, kb::math::Vec3 worldPoint) noexcept;
[[nodiscard]] bool SceneVisibilityCellApplies(const VisibilityCellComponent& cell, std::uint32_t membershipMask) noexcept;

} // namespace kb::scene

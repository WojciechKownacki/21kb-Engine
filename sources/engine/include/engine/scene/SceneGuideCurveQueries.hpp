#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

// Evaluates a normalized parameter. Open curves clamp to [0, 1]; closed
// curves wrap it. Returns false for disabled or malformed curve data.
[[nodiscard]] bool GuideCurveEvaluateLocal(const GuideCurveComponent& curve, float parameter, Vec3& position, Vec3& tangent) noexcept;
[[nodiscard]] bool SceneGuideCurveEvaluate(const Scene& scene, SceneEntity entity, float parameter, Vec3& position, Vec3& tangent) noexcept;
// The same with the world position in double precision: the curve's double-precision translation plus its local
// point, so a curve far from the world origin keeps float precision along itself (docs/large_worlds.md).
[[nodiscard]] bool SceneGuideCurveEvaluate(const Scene& scene, SceneEntity entity, float parameter, kb::math::DVec3& position, Vec3& tangent) noexcept;

} // namespace kb::scene

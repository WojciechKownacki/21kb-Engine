#pragma once

#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

// Evaluates a normalized parameter. Open curves clamp to [0, 1]; closed
// curves wrap it. Returns false for disabled or malformed curve data.
[[nodiscard]] bool GuideCurveEvaluateLocal(const GuideCurveComponent& curve, float parameter, Vec3& position, Vec3& tangent) noexcept;
[[nodiscard]] bool SceneGuideCurveEvaluate(const Scene& scene, SceneEntity entity, float parameter, Vec3& position, Vec3& tangent) noexcept;

} // namespace kb::scene

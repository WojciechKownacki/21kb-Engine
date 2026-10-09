#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

// Local-space containment is allocation-free and is the common consumer for
// systems which already own their query point in a region's local frame.
[[nodiscard]] bool RegionShapeContainsLocal(const RegionShapeComponent& shape, Vec3 localPoint) noexcept;

// World-space containment resolves the owner's transform once, transforms the
// point into the shape's local space and delegates to the canonical local
// primitive implementation.
[[nodiscard]] bool SceneRegionShapeContains(const Scene& scene, SceneEntity entity, Vec3 worldPoint) noexcept;
// The same for a double-precision world point, measured from the shape's double-precision translation, so a
// region far from the world origin keeps float precision around itself (docs/large_worlds.md).
[[nodiscard]] bool SceneRegionShapeContains(const Scene& scene, SceneEntity entity, const kb::math::DVec3& worldPoint) noexcept;

} // namespace kb::scene

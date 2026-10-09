#pragma once

#include "engine/math/DVec3.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class SceneState;

// Double-precision translations of scene entities (docs/large_worlds.md, SceneTransformResiduals).
class SceneTransformPrecision {
public:
    SceneTransformPrecision() = delete;

    // The part of the entity's local translation below float precision: zero without one, or once the float
    // view moved too far from it.
    [[nodiscard]] static Vec3 LocalResidual(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept;
    // Stores the local residual of a live entity. A zero residual for an entity without an entry stores nothing.
    // Not thread-safe (like a transform write).
    static void StoreLocalResidual(SceneState& state, SceneEntity entity, Vec3 residual);
    [[nodiscard]] static kb::math::DVec3 LocalTranslation(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept;
    // The composed world translation (fresh after the transform sync, like worldPosition).
    [[nodiscard]] static kb::math::DVec3 WorldTranslation(const SceneState& state, SceneEntity entity, const TransformComponent& transform) noexcept;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitors.hpp"
#include "engine/scene/TransformComponent.hpp"

namespace kb::scene {

class Scene;

class SceneSystemTransformAccess {
public:
    explicit SceneSystemTransformAccess(Scene& scene) noexcept;

    [[nodiscard]] bool IsAlive(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent Get(SceneEntity entity) const;
    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TransformComponent& transform);
    void MarkModified(SceneEntity entity) noexcept;

    void ForEach(ConstTransformVisitor visitor, void* context = nullptr) const;
    void ForEachMutable(MutableTransformVisitor visitor, void* context = nullptr);

    // Double-precision translations (docs/large_worlds.md). localPosition/worldPosition are these rounded to float;
    // a missing entity reads as zero. The world translation is fresh after the transform sync, like worldPosition.
    [[nodiscard]] kb::math::DVec3 LocalTranslation(SceneEntity entity) const noexcept;
    [[nodiscard]] kb::math::DVec3 WorldTranslation(SceneEntity entity) const noexcept;
    // The same for a row the caller already holds (TryGet, a visitor), without a second lookup.
    [[nodiscard]] kb::math::DVec3 LocalTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept;
    [[nodiscard]] kb::math::DVec3 WorldTranslation(SceneEntity entity, const TransformComponent& transform) const noexcept;
    // Writes the entity's local translation in double precision, the way Set writes a transform (versions, world
    // composition, dirty tracking). Float writes of localPosition keep a stored residual while it fits.
    void SetLocalTranslation(SceneEntity entity, const kb::math::DVec3& translation);

private:
    Scene& scene_;
};

} // namespace kb::scene

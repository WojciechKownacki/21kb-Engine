#pragma once

#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneColliderComponentQueries {
public:
    explicit SceneColliderComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ColliderComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneColliderComponents {
public:
    explicit SceneColliderComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ColliderComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ColliderComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const ColliderComponent& collider);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

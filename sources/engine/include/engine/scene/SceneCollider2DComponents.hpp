#pragma once

#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneCollider2DComponentQueries {
public:
    explicit SceneCollider2DComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Collider2DComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneCollider2DComponents {
public:
    explicit SceneCollider2DComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Collider2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Collider2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Collider2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

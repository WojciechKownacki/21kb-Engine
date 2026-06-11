#pragma once

#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneRigidbodyComponentQueries {
public:
    explicit SceneRigidbodyComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RigidbodyComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneRigidbodyComponents {
public:
    explicit SceneRigidbodyComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RigidbodyComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] RigidbodyComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const RigidbodyComponent& rigidbody);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/Body2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneBody2DComponentQueries {
public:
    explicit SceneBody2DComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Body2DComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneBody2DComponents {
public:
    explicit SceneBody2DComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Body2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Body2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Body2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

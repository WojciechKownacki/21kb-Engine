#pragma once

#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneSprite2DComponentQueries {
public:
    explicit SceneSprite2DComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Sprite2DComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneSprite2DComponents {
public:
    explicit SceneSprite2DComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Sprite2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Sprite2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Sprite2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

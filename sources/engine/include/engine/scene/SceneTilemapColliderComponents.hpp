#pragma once

#include "engine/scene/TilemapColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneTilemapColliderComponentQueries {
public:
    explicit SceneTilemapColliderComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapColliderComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneTilemapColliderComponents {
public:
    explicit SceneTilemapColliderComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapColliderComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TilemapColliderComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TilemapColliderComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/TilemapComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneTilemapComponentQueries {
public:
    explicit SceneTilemapComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneTilemapComponents {
public:
    explicit SceneTilemapComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TilemapComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TilemapComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

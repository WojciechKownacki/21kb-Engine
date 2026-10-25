#pragma once

#include "engine/scene/TilemapRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneTilemapRendererComponentQueries {
public:
    explicit SceneTilemapRendererComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapRendererComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneTilemapRendererComponents {
public:
    explicit SceneTilemapRendererComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapRendererComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TilemapRendererComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TilemapRendererComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

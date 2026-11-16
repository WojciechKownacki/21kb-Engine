#pragma once

#include "engine/scene/PlanarReflectionComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class ScenePlanarReflectionComponentQueries {
public:
    explicit ScenePlanarReflectionComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PlanarReflectionComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class ScenePlanarReflectionComponents {
public:
    explicit ScenePlanarReflectionComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PlanarReflectionComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] PlanarReflectionComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const PlanarReflectionComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

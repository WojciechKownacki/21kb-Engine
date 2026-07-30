#pragma once

#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneRegionShapeComponentQueries {
public:
    explicit SceneRegionShapeComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RegionShapeComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneRegionShapeComponents {
public:
    explicit SceneRegionShapeComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RegionShapeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] RegionShapeComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const RegionShapeComponent& shape);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

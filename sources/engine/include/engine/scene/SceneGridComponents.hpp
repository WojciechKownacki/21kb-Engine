#pragma once

#include "engine/scene/GridComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneGridComponentQueries {
public:
    explicit SceneGridComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GridComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneGridComponents {
public:
    explicit SceneGridComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GridComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GridComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const GridComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

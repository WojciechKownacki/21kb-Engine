#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"

namespace kb::scene {

class Scene;

class SceneWorldBackdropComponentQueries {
public:
    explicit SceneWorldBackdropComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const WorldBackdropComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneWorldBackdropComponents {
public:
    explicit SceneWorldBackdropComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const WorldBackdropComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] WorldBackdropComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const WorldBackdropComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

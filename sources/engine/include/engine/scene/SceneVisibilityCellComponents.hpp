#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

namespace kb::scene {

class Scene;

class SceneVisibilityCellComponentQueries {
public:
    explicit SceneVisibilityCellComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const VisibilityCellComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneVisibilityCellComponents {
public:
    explicit SceneVisibilityCellComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const VisibilityCellComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityCellComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const VisibilityCellComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

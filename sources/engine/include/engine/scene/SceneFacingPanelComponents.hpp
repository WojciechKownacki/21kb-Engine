#pragma once

#include "engine/scene/FacingPanelComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;
using FacingPanelVisitor = void (*)(SceneEntity entity, const FacingPanelComponent& component, void* context);

class SceneFacingPanelComponentQueries {
public:
    explicit SceneFacingPanelComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const FacingPanelComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(FacingPanelVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneFacingPanelComponents {
public:
    explicit SceneFacingPanelComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const FacingPanelComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] FacingPanelComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const FacingPanelComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneLightComponentQueries {
public:
    explicit SceneLightComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LightComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneLightComponents {
public:
    explicit SceneLightComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LightComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] LightComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const LightComponent& light);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/RegionPortalComponent.hpp"

namespace kb::scene {

class Scene;

class SceneRegionPortalComponentQueries {
public:
    explicit SceneRegionPortalComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneRegionPortalComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneRegionPortalComponents {
public:
    explicit SceneRegionPortalComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneRegionPortalComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneRegionPortalComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneRegionPortalComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

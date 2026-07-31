#pragma once

#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneAmbientRadianceComponentQueries {
public:
    explicit SceneAmbientRadianceComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AmbientRadianceComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneAmbientRadianceComponents {
public:
    explicit SceneAmbientRadianceComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AmbientRadianceComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AmbientRadianceComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AmbientRadianceComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

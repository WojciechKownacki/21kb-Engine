#pragma once

#include "engine/scene/GuideCurveComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneGuideCurveComponentQueries {
public:
    explicit SceneGuideCurveComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GuideCurveComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneGuideCurveComponents {
public:
    explicit SceneGuideCurveComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GuideCurveComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GuideCurveComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const GuideCurveComponent& curve);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

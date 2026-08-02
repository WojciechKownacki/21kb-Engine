#pragma once

#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneDeformedGeometryComponentQueries {
public:
    explicit SceneDeformedGeometryComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const DrawD3DeformedGeometryComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneDeformedGeometryComponents {
public:
    explicit SceneDeformedGeometryComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const DrawD3DeformedGeometryComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] DrawD3DeformedGeometryComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

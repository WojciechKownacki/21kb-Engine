#pragma once

#include "engine/scene/GeometrySwarmComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;
using GeometrySwarmVisitor = void (*)(SceneEntity entity, const GeometrySwarmComponent& component, void* context);

class SceneGeometrySwarmComponentQueries {
public:
    explicit SceneGeometrySwarmComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GeometrySwarmComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(GeometrySwarmVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneGeometrySwarmComponents {
public:
    explicit SceneGeometrySwarmComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GeometrySwarmComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GeometrySwarmComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const GeometrySwarmComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

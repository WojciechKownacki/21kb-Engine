#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SurfaceCastComponent.hpp"

namespace kb::scene {

class Scene;
using SurfaceCastVisitor = void (*)(SceneEntity entity, const SurfaceCastComponent& component, void* context);

class SceneSurfaceCastComponentQueries {
public:
    explicit SceneSurfaceCastComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SurfaceCastComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(SurfaceCastVisitor visitor, void* context) const;
private:
    const Scene& scene_;
};

class SceneSurfaceCastComponents {
public:
    explicit SceneSurfaceCastComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SurfaceCastComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SurfaceCastComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SurfaceCastComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

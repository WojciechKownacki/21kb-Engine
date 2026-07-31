#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"

namespace kb::scene {

class Scene;

class SceneVisibilityBlockerComponentQueries {
public:
    explicit SceneVisibilityBlockerComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneVisibilityBlockerComponent* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneVisibilityBlockerComponents {
public:
    explicit SceneVisibilityBlockerComponents(Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneVisibilityBlockerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneVisibilityBlockerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneVisibilityBlockerComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityComponent.hpp"

namespace kb::scene {

class Scene;

class SceneVisibilityComponentQueries {
public:
    explicit SceneVisibilityComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] VisibilityComponent Get(SceneEntity entity) const;
    [[nodiscard]] const VisibilityComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneVisibilityComponents {
public:
    explicit SceneVisibilityComponents(Scene& scene) noexcept;

    [[nodiscard]] VisibilityComponent Get(SceneEntity entity) const;
    [[nodiscard]] const VisibilityComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const VisibilityComponent& visibility);
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

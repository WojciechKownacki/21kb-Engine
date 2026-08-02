#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

namespace kb::scene {

class Scene;

class SceneSkeletonBindingComponentQueries {
public:
    explicit SceneSkeletonBindingComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SkeletonBindingComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneSkeletonBindingComponents {
public:
    explicit SceneSkeletonBindingComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SkeletonBindingComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SkeletonBindingComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const SkeletonBindingComponent& binding);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

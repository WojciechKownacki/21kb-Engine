#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneAnimatorComponentQueries {
public:
    explicit SceneAnimatorComponentQueries(const Scene& scene) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Animator* TryGet(SceneEntity entity) const noexcept;
private:
    const Scene& scene_;
};

class SceneAnimatorComponents {
public:
    explicit SceneAnimatorComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Animator* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Animator* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Animator& animator);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

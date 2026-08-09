#pragma once

#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneMotionSkeletonRuleComponentQueries {
public:
    explicit SceneMotionSkeletonRuleComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MotionSkeletonRuleComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneMotionSkeletonRuleComponents {
public:
    explicit SceneMotionSkeletonRuleComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MotionSkeletonRuleComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] MotionSkeletonRuleComponent* TryGet(SceneEntity entity) noexcept;
    [[nodiscard]] bool Set(SceneEntity entity, const MotionSkeletonRuleComponent& rule);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

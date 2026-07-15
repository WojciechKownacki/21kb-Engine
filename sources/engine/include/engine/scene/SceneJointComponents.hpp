#pragma once

#include "engine/scene/JointComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::scene {

class Scene;

class SceneJointComponentQueries {
public:
    explicit SceneJointComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const JointComponent* TryGet(SceneEntity entity) const noexcept;

private:
    const Scene& scene_;
};

class SceneJointComponents {
public:
    explicit SceneJointComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const JointComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] JointComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const JointComponent& joint);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

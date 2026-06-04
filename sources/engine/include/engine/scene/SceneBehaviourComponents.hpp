#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SceneVisitorTypes.hpp"

namespace kb::scene {

class Scene;

class SceneBehaviourComponentQueries {
public:
    explicit SceneBehaviourComponentQueries(const Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    void ForEach(BehaviourVisitor visitor, void* context = nullptr) const;

private:
    const Scene& scene_;
};

class SceneBehaviourComponents {
public:
    explicit SceneBehaviourComponents(Scene& scene) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] BehaviourComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const BehaviourComponent& behaviour);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
    void ForEach(BehaviourVisitor visitor, void* context = nullptr) const;

private:
    Scene& scene_;
};

} // namespace kb::scene

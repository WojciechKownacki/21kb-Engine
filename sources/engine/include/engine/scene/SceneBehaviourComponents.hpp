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
    // Moves whenever a behaviour is set, removed or marked modified (the call that follows a write through TryGet), or
    // an entity that carries one is destroyed. The counter is read directly, so a loop can compare it after every
    // script call.
    [[nodiscard]] const std::uint64_t* RevisionCounter() const noexcept;
    // Moves for every change to the scene's behaviours, including those that bypass the calls above. One value to
    // compare before visiting any of them.
    [[nodiscard]] std::uint64_t ChangeStamp() const noexcept;

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
    // See SceneBehaviourComponentQueries.
    [[nodiscard]] const std::uint64_t* RevisionCounter() const noexcept;
    [[nodiscard]] std::uint64_t ChangeStamp() const noexcept;

private:
    Scene& scene_;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneBehaviourComponentStore {
public:
    SceneBehaviourComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const BehaviourComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] BehaviourComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const BehaviourComponent& behaviour);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

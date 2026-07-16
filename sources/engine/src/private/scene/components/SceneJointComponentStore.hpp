#pragma once

#include "engine/scene/JointComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneJointComponentStore {
public:
    SceneJointComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const JointComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] JointComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const JointComponent& joint);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

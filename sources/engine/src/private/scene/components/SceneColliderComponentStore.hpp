#pragma once

#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneColliderComponentStore {
public:
    SceneColliderComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ColliderComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ColliderComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const ColliderComponent& collider);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

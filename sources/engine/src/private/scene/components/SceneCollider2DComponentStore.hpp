#pragma once

#include "engine/scene/Collider2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneCollider2DComponentStore {
public:
    SceneCollider2DComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Collider2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Collider2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Collider2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

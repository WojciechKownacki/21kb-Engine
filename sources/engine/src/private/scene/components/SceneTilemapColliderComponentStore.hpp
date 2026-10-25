#pragma once

#include "engine/scene/TilemapColliderComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneTilemapColliderComponentStore {
public:
    SceneTilemapColliderComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapColliderComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TilemapColliderComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TilemapColliderComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

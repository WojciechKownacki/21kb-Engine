#pragma once

#include "engine/scene/TilemapComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneTilemapComponentStore {
public:
    SceneTilemapComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TilemapComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TilemapComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TilemapComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

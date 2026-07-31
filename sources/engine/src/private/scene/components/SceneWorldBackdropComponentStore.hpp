#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/WorldBackdropComponent.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneWorldBackdropComponentStore {
public:
    SceneWorldBackdropComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const WorldBackdropComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] WorldBackdropComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const WorldBackdropComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

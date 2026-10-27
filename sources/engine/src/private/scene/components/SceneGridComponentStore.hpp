#pragma once

#include "engine/scene/GridComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneGridComponentStore {
public:
    SceneGridComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GridComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GridComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const GridComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/RegionShapeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneRegionShapeComponentStore {
public:
    SceneRegionShapeComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RegionShapeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] RegionShapeComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const RegionShapeComponent& shape);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/RegionPortalComponent.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneRegionPortalComponentStore {
public:
    SceneRegionPortalComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneRegionPortalComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneRegionPortalComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneRegionPortalComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

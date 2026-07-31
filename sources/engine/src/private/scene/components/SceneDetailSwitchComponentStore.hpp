#pragma once

#include "engine/scene/DetailSwitchComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneDetailSwitchComponentStore {
public:
    SceneDetailSwitchComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneDetailSwitchComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneDetailSwitchComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneDetailSwitchComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

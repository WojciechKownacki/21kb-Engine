#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/StreamFocusComponent.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneStreamFocusComponentStore {
public:
    SceneStreamFocusComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const StreamFocusComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] StreamFocusComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const StreamFocusComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

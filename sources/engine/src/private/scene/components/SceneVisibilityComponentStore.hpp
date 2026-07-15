#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneVisibilityComponentStore {
public:
    SceneVisibilityComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] const VisibilityComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const VisibilityComponent& visibility);
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

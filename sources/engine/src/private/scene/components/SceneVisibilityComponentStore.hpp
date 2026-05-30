#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityComponent.hpp"

#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneVisibilityComponentStore {
public:
    SceneVisibilityComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept;

    [[nodiscard]] const VisibilityComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const VisibilityComponent& visibility);
    void MarkModified(SceneEntity entity) noexcept;

private:
    ecs_world_t* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

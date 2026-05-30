#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneTransformComponentStore {
public:
    SceneTransformComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept;

    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TransformComponent& transform);
    void MarkModified(SceneEntity entity) noexcept;

private:
    ecs_world_t* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

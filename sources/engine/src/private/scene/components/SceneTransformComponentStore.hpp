#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneTransformComponentStore {
public:
    SceneTransformComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] const TransformComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TransformComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TransformComponent& transform);
    void MarkModified(SceneEntity entity) noexcept;
    void MarkParentModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

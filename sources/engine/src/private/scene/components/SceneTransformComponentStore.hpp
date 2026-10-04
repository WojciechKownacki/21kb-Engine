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
    // The row Set stores for `transform` over the row `current` (null for a new one): the versions carry on.
    [[nodiscard]] static TransformComponent Written(const TransformComponent* current, const TransformComponent& transform) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
    // Publishes a row that was written in place (versions untouched): flags it and mirrors it to the backend.
    void MarkWritten(SceneEntity entity) noexcept;
    void MarkParentModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0U;
};

} // namespace kb::scene

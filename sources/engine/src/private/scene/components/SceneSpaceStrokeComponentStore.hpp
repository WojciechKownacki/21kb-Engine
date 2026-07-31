#pragma once

#include "engine/scene/SceneSpaceStrokeComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneSpaceStrokeComponentStore {
public:
    SceneSpaceStrokeComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SpaceStrokeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SpaceStrokeComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(SpaceStrokeVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const SpaceStrokeComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityBlockerComponent.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneVisibilityBlockerComponentStore {
public:
    SceneVisibilityBlockerComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SceneVisibilityBlockerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SceneVisibilityBlockerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SceneVisibilityBlockerComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

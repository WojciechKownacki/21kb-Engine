#pragma once

#include "engine/scene/ContentInstanceComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneContentInstanceComponentStore {
public:
    SceneContentInstanceComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const ContentInstanceComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] ContentInstanceComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const ContentInstanceComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

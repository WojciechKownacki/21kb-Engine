#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/VisibilityCellComponent.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneVisibilityCellComponentStore {
public:
    SceneVisibilityCellComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const VisibilityCellComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] VisibilityCellComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const VisibilityCellComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/SceneRiverComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneRiverComponentStore {
public:
    SceneRiverComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const RiverComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] RiverComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(RiverVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const RiverComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

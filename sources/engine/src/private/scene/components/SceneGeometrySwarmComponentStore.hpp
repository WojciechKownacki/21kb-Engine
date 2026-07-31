#pragma once

#include "engine/scene/SceneGeometrySwarmComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneGeometrySwarmComponentStore {
public:
    SceneGeometrySwarmComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const GeometrySwarmComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] GeometrySwarmComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(GeometrySwarmVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const GeometrySwarmComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

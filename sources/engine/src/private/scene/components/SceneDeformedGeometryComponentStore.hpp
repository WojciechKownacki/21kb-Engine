#pragma once

#include "engine/scene/DrawD3DeformedGeometryComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneDeformedGeometryComponentStore {
public:
    SceneDeformedGeometryComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const DrawD3DeformedGeometryComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] DrawD3DeformedGeometryComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const DrawD3DeformedGeometryComponent& geometry);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

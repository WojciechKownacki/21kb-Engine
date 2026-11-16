#pragma once

#include "engine/scene/PlanarReflectionComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class ScenePlanarReflectionComponentStore {
public:
    ScenePlanarReflectionComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PlanarReflectionComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] PlanarReflectionComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const PlanarReflectionComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

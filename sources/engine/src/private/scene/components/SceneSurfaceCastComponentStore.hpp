#pragma once

#include "engine/scene/SceneSurfaceCastComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneSurfaceCastComponentStore {
public:
    SceneSurfaceCastComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SurfaceCastComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SurfaceCastComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(SurfaceCastVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const SurfaceCastComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

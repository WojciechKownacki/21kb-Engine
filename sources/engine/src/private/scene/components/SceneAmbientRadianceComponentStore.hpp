#pragma once

#include "engine/scene/AmbientRadianceComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneAmbientRadianceComponentStore {
public:
    SceneAmbientRadianceComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AmbientRadianceComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AmbientRadianceComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AmbientRadianceComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

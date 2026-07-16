#pragma once

#include "engine/scene/LightComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneLightComponentStore {
public:
    SceneLightComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LightComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] LightComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const LightComponent& light);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

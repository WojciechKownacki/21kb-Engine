#pragma once

#include "engine/scene/CharacterController2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneCharacterController2DComponentStore {
public:
    SceneCharacterController2DComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterController2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CharacterController2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CharacterController2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

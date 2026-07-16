#pragma once

#include "engine/scene/CharacterControllerComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneCharacterControllerComponentStore {
public:
    SceneCharacterControllerComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CharacterControllerComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CharacterControllerComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CharacterControllerComponent& characterController);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

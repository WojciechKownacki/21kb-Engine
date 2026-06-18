#pragma once

#include "engine/scene/InputComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneInputComponentStore {
public:
    SceneInputComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const InputComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] InputComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const InputComponent& input);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

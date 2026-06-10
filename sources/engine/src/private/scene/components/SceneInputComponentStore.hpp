#pragma once

#include "engine/scene/InputComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneInputComponentStore {
public:
    SceneInputComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const InputComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] InputComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const InputComponent& input);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    ecs_world_t* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

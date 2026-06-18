#pragma once

#include "engine/scene/CameraComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneCameraComponentStore {
public:
    SceneCameraComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const CameraComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] CameraComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const CameraComponent& camera);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

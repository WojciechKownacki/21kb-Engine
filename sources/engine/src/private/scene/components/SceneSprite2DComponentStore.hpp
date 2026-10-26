#pragma once

#include "engine/scene/Sprite2DComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneSprite2DComponentStore {
public:
    SceneSprite2DComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Sprite2DComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Sprite2DComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Sprite2DComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

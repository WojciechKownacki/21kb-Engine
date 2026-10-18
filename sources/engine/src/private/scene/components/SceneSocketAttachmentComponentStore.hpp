#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SocketAttachmentComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneSocketAttachmentComponentStore {
public:
    SceneSocketAttachmentComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SocketAttachmentComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SocketAttachmentComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SocketAttachmentComponent& attachment);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

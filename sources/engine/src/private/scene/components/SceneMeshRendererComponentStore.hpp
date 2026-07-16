#pragma once

#include "engine/scene/MeshRendererComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneMeshRendererComponentStore {
public:
    SceneMeshRendererComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MeshRendererComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] MeshRendererComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const MeshRendererComponent& renderer);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

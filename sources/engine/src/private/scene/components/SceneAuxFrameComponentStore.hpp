#pragma once

#include "engine/scene/SceneAuxFrameComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneAuxFrameComponentStore {
public:
    SceneAuxFrameComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AuxFrameComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AuxFrameComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(AuxFrameVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const AuxFrameComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

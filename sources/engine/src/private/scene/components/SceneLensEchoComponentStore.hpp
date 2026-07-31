#pragma once

#include "engine/scene/SceneLensEchoComponents.hpp"

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneLensEchoComponentStore {
public:
    SceneLensEchoComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const LensEchoComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] LensEchoComponent* TryGet(SceneEntity entity) noexcept;
    void ForEach(LensEchoVisitor visitor, void* context) const;
    void Set(SceneEntity entity, const LensEchoComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

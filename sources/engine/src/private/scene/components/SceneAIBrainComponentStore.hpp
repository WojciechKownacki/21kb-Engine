#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/AIBrainComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneAIBrainComponentStore {
public:
    SceneAIBrainComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const AIBrainComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] AIBrainComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const AIBrainComponent& aiBrain);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

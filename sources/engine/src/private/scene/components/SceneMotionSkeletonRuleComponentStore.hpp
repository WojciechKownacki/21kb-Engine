#pragma once

#include "engine/scene/MotionSkeletonRuleComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneMotionSkeletonRuleComponentStore {
public:
    SceneMotionSkeletonRuleComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const MotionSkeletonRuleComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] MotionSkeletonRuleComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const MotionSkeletonRuleComponent& rule);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

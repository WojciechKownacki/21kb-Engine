#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/SkeletonBindingComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneSkeletonBindingComponentStore {
public:
    SceneSkeletonBindingComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const SkeletonBindingComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] SkeletonBindingComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const SkeletonBindingComponent& binding);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

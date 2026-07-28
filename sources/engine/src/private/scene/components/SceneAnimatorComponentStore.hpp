#pragma once

#include "engine/scene/AnimationAssets.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneAnimatorComponentStore {
public:
    SceneAnimatorComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const Animator* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] Animator* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const Animator& animator);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

#pragma once

#include "engine/scene/PostProcessVolumeComponent.hpp"
#include "engine/scene/SceneEntity.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class ScenePostProcessVolumeComponentStore {
public:
    ScenePostProcessVolumeComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;
    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const PostProcessVolumeComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] PostProcessVolumeComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const PostProcessVolumeComponent& component);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;
private:
    kb::ecs::World* world_ = nullptr;
};

} // namespace kb::scene

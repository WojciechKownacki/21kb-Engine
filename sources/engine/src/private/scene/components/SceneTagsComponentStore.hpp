#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TagsComponent.hpp"

#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneTagsComponentStore {
public:
    SceneTagsComponentStore(ecs_world_t* world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TagsComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TagsComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TagsComponent& tags);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    ecs_world_t* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

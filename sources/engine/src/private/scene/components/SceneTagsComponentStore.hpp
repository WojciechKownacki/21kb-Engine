#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "engine/scene/TagsComponent.hpp"

#include <cstdint>

namespace kb::ecs { class World; }

namespace kb::scene {

class SceneTagsComponentStore {
public:
    SceneTagsComponentStore(kb::ecs::World& world, std::uint64_t componentId) noexcept;

    [[nodiscard]] bool Has(SceneEntity entity) const noexcept;
    [[nodiscard]] const TagsComponent* TryGet(SceneEntity entity) const noexcept;
    [[nodiscard]] TagsComponent* TryGet(SceneEntity entity) noexcept;
    void Set(SceneEntity entity, const TagsComponent& tags);
    void Remove(SceneEntity entity) noexcept;
    void MarkModified(SceneEntity entity) noexcept;

private:
    kb::ecs::World* world_ = nullptr;
    std::uint64_t componentId_ = 0;
};

} // namespace kb::scene

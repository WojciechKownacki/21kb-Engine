#pragma once

#include "engine/scene/SceneEntity.hpp"

#include <cstddef>
#include <cstdint>

struct ecs_world_t;

namespace kb::scene {

class SceneComponentAccess {
public:
    SceneComponentAccess() = delete;

    [[nodiscard]] static const void* TryGet(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept;
    [[nodiscard]] static void* TryGetMutable(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept;
    [[nodiscard]] static bool Has(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept;

    static void Set(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId, std::size_t size, const void* value);
    static void Remove(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept;
    static void MarkModified(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept;
};

} // namespace kb::scene

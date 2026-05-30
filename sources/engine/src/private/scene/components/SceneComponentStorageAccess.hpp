#pragma once

#include "scene/components/SceneComponentAccess.hpp"

namespace kb::scene {

class SceneComponentStorageAccess {
public:
    SceneComponentStorageAccess() = delete;

    template <typename T>
    [[nodiscard]] static const T* TryGet(const ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
        return static_cast<const T*>(SceneComponentAccess::TryGet(world, entity, componentId));
    }

    template <typename T>
    [[nodiscard]] static T* TryGetMutable(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId) noexcept {
        return static_cast<T*>(SceneComponentAccess::TryGetMutable(world, entity, componentId));
    }

    template <typename T>
    static void Set(ecs_world_t* world, SceneEntity entity, std::uint64_t componentId, const T& component) {
        SceneComponentAccess::Set(world, entity, componentId, sizeof(T), &component);
    }
};

} // namespace kb::scene

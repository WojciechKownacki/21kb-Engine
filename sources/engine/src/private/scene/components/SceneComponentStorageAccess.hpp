#pragma once

#include "engine/ecs/World.hpp"
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

    template <typename T>
    [[nodiscard]] static bool Has(const kb::ecs::World* world, SceneEntity entity) noexcept {
        return world != nullptr && entity.IsValid() && world->Has<T>(entity);
    }

    template <typename T>
    [[nodiscard]] static const T* TryGet(const kb::ecs::World* world, SceneEntity entity) noexcept {
        return world == nullptr || !entity.IsValid() ? nullptr : world->TryGet<T>(entity);
    }

    template <typename T>
    [[nodiscard]] static T* TryGetMutable(kb::ecs::World* world, SceneEntity entity) noexcept {
        return world == nullptr || !entity.IsValid() ? nullptr : world->TryGetMutable<T>(entity);
    }

    template <typename T>
    static void Set(kb::ecs::World* world, SceneEntity entity, const T& component) {
        if (world != nullptr && entity.IsValid()) {
            world->Set<T>(entity, component);
        }
    }

    template <typename T>
    static void Remove(kb::ecs::World* world, SceneEntity entity) noexcept {
        if (world != nullptr && entity.IsValid()) {
            world->Remove<T>(entity);
        }
    }

    template <typename T>
    static void MarkModified(kb::ecs::World* world, SceneEntity entity) noexcept {
        if (world != nullptr && entity.IsValid()) {
            world->MarkModified<T>(entity);
        }
    }
};

} // namespace kb::scene

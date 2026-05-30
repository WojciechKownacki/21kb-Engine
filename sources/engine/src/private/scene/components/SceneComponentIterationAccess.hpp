#pragma once

#include "engine/scene/SceneEntity.hpp"
#include "scene/components/SceneComponentAccess.hpp"

#include <cstdint>

struct ecs_iter_t;

namespace kb::scene {

class SceneComponentIterationAccess {
public:
    SceneComponentIterationAccess() = delete;

    template <typename T>
    [[nodiscard]] static const T* Field(const ecs_iter_t& it, std::size_t fieldIndex) noexcept {
        return static_cast<const T*>(ecs_field_w_size(&it, sizeof(T), static_cast<int8_t>(fieldIndex)));
    }

    template <typename T>
    [[nodiscard]] static T* MutableField(const ecs_iter_t& it, std::size_t fieldIndex) noexcept {
        return static_cast<T*>(ecs_field_w_size(&it, sizeof(T), static_cast<int8_t>(fieldIndex)));
    }

    template <typename T>
    [[nodiscard]] static const T* TryGet(const kb::ecs::World& world, SceneEntity entity, std::uint64_t componentId) noexcept {
        return static_cast<const T*>(SceneComponentAccess::TryGet(world.NativeHandle(), entity, componentId));
    }
};

} // namespace kb::scene

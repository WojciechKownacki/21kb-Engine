#pragma once

#include <string_view>
#include <type_traits>
#include <typeinfo>

namespace kb::ecs {

template <typename T>
constexpr void World::ValidateComponentType() noexcept {
    static_assert(std::is_object_v<T>, "ECS component must be an object type");
    static_assert(std::is_trivially_copyable_v<T>, "ECS component must be trivially copyable until component lifecycle hooks are exposed");
}

template <typename T>
std::string_view World::DefaultComponentName() noexcept {
    return typeid(T).name();
}

template <typename T>
constexpr void World::ValidateTagType() noexcept {
    static_assert(std::is_empty_v<T>, "ECS tag/relation marker types must be empty types");
}

template <typename T>
std::string_view World::DefaultTagName() noexcept {
    return typeid(T).name();
}

} // namespace kb::ecs

#pragma once

#include <typeindex>

namespace kb::ecs {

template <typename T>
TagId World::RegisterTag(std::string_view name) {
    ValidateTagType<T>();
    return RegisterTag(std::type_index{ typeid(T) }, name.empty() ? DefaultTagName<T>() : name);
}

template <typename T>
TagId World::Tag() const noexcept {
    ValidateTagType<T>();
    return FindTag(std::type_index{ typeid(T) });
}

template <typename T>
void World::AddTag(Entity entity) {
    AddTag(entity, RegisterTag<T>());
}

template <typename T>
bool World::HasTag(Entity entity) const {
    ValidateEntityHandle(entity, "HasTag");
    const TagId tag = Tag<T>();
    return tag != 0 && HasTag(entity, tag);
}

template <typename T>
void World::RemoveTag(Entity entity) {
    ValidateEntityHandle(entity, "RemoveTag");
    const TagId tag = Tag<T>();
    if (tag != 0) {
        RemoveTag(entity, tag);
    }
}

} // namespace kb::ecs

#pragma once

#include <typeindex>

namespace kb::ecs {

template <typename T>
ComponentId World::RegisterComponent(std::string_view name) {
    ValidateComponentType<T>();
    return RegisterComponent(
        std::type_index{ typeid(T) },
        name.empty() ? DefaultComponentName<T>() : name,
        sizeof(T),
        alignof(T));
}

template <typename T>
ComponentId World::Component() const noexcept {
    ValidateComponentType<T>();
    return FindComponent(std::type_index{ typeid(T) });
}

template <typename T>
void World::Set(Entity entity, const T& component) {
    const ComponentId componentId = RegisterComponent<T>();
    SetComponent(entity, componentId, sizeof(T), &component);
}

template <typename T>
bool World::Has(Entity entity) const noexcept {
    const ComponentId componentId = Component<T>();
    return componentId != 0 && HasComponent(entity, componentId);
}

template <typename T>
const T* World::TryGet(Entity entity) const noexcept {
    const ComponentId componentId = Component<T>();
    return componentId == 0 ? nullptr : static_cast<const T*>(TryGetComponent(entity, componentId));
}

template <typename T>
T* World::TryGetMutable(Entity entity) noexcept {
    const ComponentId componentId = Component<T>();
    return componentId == 0 ? nullptr : static_cast<T*>(TryGetMutableComponent(entity, componentId));
}

template <typename T>
void World::Remove(Entity entity) noexcept {
    const ComponentId componentId = Component<T>();
    if (componentId != 0) {
        RemoveComponent(entity, componentId);
    }
}

template <typename T>
void World::MarkModified(Entity entity) noexcept {
    const ComponentId componentId = Component<T>();
    if (componentId != 0) {
        MarkComponentModified(entity, componentId);
    }
}

} // namespace kb::ecs

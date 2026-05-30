#pragma once

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

template <typename T>
void World::ForEach(ConstComponentVisitor<T> visitor, void* context) const {
    if (visitor == nullptr) {
        return;
    }

    struct AdapterContext {
        ConstComponentVisitor<T> visitor = nullptr;
        void* context = nullptr;
    } adapterContext{ visitor, context };

    const ComponentId componentId = Component<T>();
    if (componentId == 0) {
        return;
    }

    ForEachComponent(
        componentId,
        sizeof(T),
        [](Entity entity, const void* component, void* adapter) {
            auto* typedContext = static_cast<AdapterContext*>(adapter);
            typedContext->visitor(entity, *static_cast<const T*>(component), typedContext->context);
        },
        &adapterContext);
}

template <typename T>
void World::ForEachMutable(MutableComponentVisitor<T> visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    struct AdapterContext {
        MutableComponentVisitor<T> visitor = nullptr;
        void* context = nullptr;
    } adapterContext{ visitor, context };

    const ComponentId componentId = Component<T>();
    if (componentId == 0) {
        return;
    }

    ForEachMutableComponent(
        componentId,
        sizeof(T),
        [](Entity entity, void* component, void* adapter) {
            auto* typedContext = static_cast<AdapterContext*>(adapter);
            typedContext->visitor(entity, *static_cast<T*>(component), typedContext->context);
        },
        &adapterContext);
}

template <typename TFirst, typename TSecond>
void World::ForEach(ConstComponentsVisitor<TFirst, TSecond> visitor, void* context) const {
    ValidateComponentType<TFirst>();
    ValidateComponentType<TSecond>();

    if (visitor == nullptr) {
        return;
    }

    struct AdapterContext {
        ConstComponentsVisitor<TFirst, TSecond> visitor = nullptr;
        void* context = nullptr;
    } adapterContext{ visitor, context };

    const ComponentId firstComponentId = Component<TFirst>();
    const ComponentId secondComponentId = Component<TSecond>();
    if (firstComponentId == 0 || secondComponentId == 0) {
        return;
    }

    ForEachComponents(
        firstComponentId,
        sizeof(TFirst),
        secondComponentId,
        sizeof(TSecond),
        [](Entity entity, const void* first, const void* second, void* adapter) {
            auto* typedContext = static_cast<AdapterContext*>(adapter);
            typedContext->visitor(entity, *static_cast<const TFirst*>(first), *static_cast<const TSecond*>(second), typedContext->context);
        },
        &adapterContext);
}

} // namespace kb::ecs

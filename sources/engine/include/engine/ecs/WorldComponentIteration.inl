#pragma once

namespace kb::ecs {

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

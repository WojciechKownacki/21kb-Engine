#pragma once

#include "engine/ecs/world/WorldComponentIterationAdapters.inl"

namespace kb::ecs {

template <typename T>
void World::ForEach(ConstComponentVisitor<T> visitor, void* context) const {
    if (visitor == nullptr) {
        return;
    }

    const ComponentId componentId = Component<T>();
    if (componentId == 0) {
        return;
    }

    ConstComponentIterationAdapter<T> adapter{ visitor, context };
    ForEachComponent(
        componentId,
        sizeof(T),
        &ConstComponentIterationAdapter<T>::Dispatch,
        &adapter);
}

template <typename T>
void World::ForEachMutable(MutableComponentVisitor<T> visitor, void* context) {
    if (visitor == nullptr) {
        return;
    }

    const ComponentId componentId = Component<T>();
    if (componentId == 0) {
        return;
    }

    MutableComponentIterationAdapter<T> adapter{ visitor, context };
    ForEachMutableComponent(
        componentId,
        sizeof(T),
        &MutableComponentIterationAdapter<T>::Dispatch,
        &adapter);
}

template <typename TFirst, typename TSecond>
void World::ForEach(ConstComponentsVisitor<TFirst, TSecond> visitor, void* context) const {
    ValidateComponentType<TFirst>();
    ValidateComponentType<TSecond>();

    if (visitor == nullptr) {
        return;
    }

    const ComponentId firstComponentId = Component<TFirst>();
    const ComponentId secondComponentId = Component<TSecond>();
    if (firstComponentId == 0 || secondComponentId == 0) {
        return;
    }

    ConstComponentPairIterationAdapter<TFirst, TSecond> adapter{ visitor, context };
    ForEachComponents(
        firstComponentId,
        sizeof(TFirst),
        secondComponentId,
        sizeof(TSecond),
        &ConstComponentPairIterationAdapter<TFirst, TSecond>::Dispatch,
        &adapter);
}

} // namespace kb::ecs

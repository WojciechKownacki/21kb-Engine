#pragma once

namespace kb::ecs {

template <typename T>
struct ConstComponentIterationAdapter {
    void (*visitor)(Entity entity, const T& component, void* context) = nullptr;
    void* context = nullptr;

    static void Dispatch(Entity entity, const void* component, void* adapter) {
        auto* typedAdapter = static_cast<ConstComponentIterationAdapter*>(adapter);
        typedAdapter->visitor(entity, *static_cast<const T*>(component), typedAdapter->context);
    }
};

template <typename T>
struct MutableComponentIterationAdapter {
    void (*visitor)(Entity entity, T& component, void* context) = nullptr;
    void* context = nullptr;

    static void Dispatch(Entity entity, void* component, void* adapter) {
        auto* typedAdapter = static_cast<MutableComponentIterationAdapter*>(adapter);
        typedAdapter->visitor(entity, *static_cast<T*>(component), typedAdapter->context);
    }
};

template <typename TFirst, typename TSecond>
struct ConstComponentPairIterationAdapter {
    void (*visitor)(Entity entity, const TFirst& first, const TSecond& second, void* context) = nullptr;
    void* context = nullptr;

    static void Dispatch(Entity entity, const void* first, const void* second, void* adapter) {
        auto* typedAdapter = static_cast<ConstComponentPairIterationAdapter*>(adapter);
        typedAdapter->visitor(entity, *static_cast<const TFirst*>(first), *static_cast<const TSecond*>(second), typedAdapter->context);
    }
};

} // namespace kb::ecs

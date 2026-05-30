#pragma once

#include <new>

namespace kb::ecs {

template <typename T>
ObserverId World::ObserveComponent(ComponentEventKind event, ComponentEventVisitor<T> visitor, void* context, bool yieldExisting) {
    ValidateComponentType<T>();
    if (visitor == nullptr) {
        return 0;
    }

    struct Binding {
        ComponentEventVisitor<T> visitor = nullptr;
        void* context = nullptr;
    };

    auto* binding = new (std::nothrow) Binding{ visitor, context };
    if (binding == nullptr) {
        return 0;
    }

    const ComponentId componentId = RegisterComponent<T>();
    return ObserveComponent(
        componentId,
        sizeof(T),
        event,
        [](Entity entity, ComponentEventKind eventKind, const void* component, void* visitorContext) {
            auto* typedContext = static_cast<Binding*>(visitorContext);
            typedContext->visitor(entity, eventKind, static_cast<const T*>(component), typedContext->context);
        },
        binding,
        [](void* visitorContext) {
            delete static_cast<Binding*>(visitorContext);
        },
        yieldExisting);
}

} // namespace kb::ecs

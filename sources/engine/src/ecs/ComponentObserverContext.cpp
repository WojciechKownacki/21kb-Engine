#include "ecs/events/ComponentObserverContext.hpp"

#include <flecs.h>

#include <cstddef>

namespace kb::ecs {

ComponentObserverContext::ComponentObserverContext(
    ComponentEventKind event,
    RawComponentObserverVisitor visitor,
    void* visitorContext,
    ComponentObserverContextFree visitorContextFree,
    std::size_t componentSize) noexcept
    : event_(event)
    , visitor_(visitor)
    , visitorContext_(visitorContext)
    , visitorContextFree_(visitorContextFree)
    , componentSize_(componentSize) {}

ComponentObserverContext::~ComponentObserverContext() {
    if (visitorContextFree_ != nullptr) {
        visitorContextFree_(visitorContext_);
    }
}

void ComponentObserverContext::Dispatch(ecs_iter_t* iterator) {
    if (iterator == nullptr) {
        return;
    }

    const auto* context = static_cast<const ComponentObserverContext*>(iterator->ctx);
    if (context != nullptr) {
        context->DispatchRows(*iterator);
    }
}

void ComponentObserverContext::Free(void* context) {
    delete static_cast<ComponentObserverContext*>(context);
}

void ComponentObserverContext::DispatchRows(ecs_iter_t& iterator) const {
    if (visitor_ == nullptr || componentSize_ == 0) {
        return;
    }

    const auto* components = static_cast<const std::byte*>(ecs_field_w_size(&iterator, static_cast<ecs_size_t>(componentSize_), 0));
    for (int32_t row = 0; row < iterator.count; ++row) {
        const void* component = components == nullptr ? nullptr : components + (static_cast<std::size_t>(row) * componentSize_);
        visitor_(Entity{ static_cast<Entity::IdType>(iterator.entities[row]) }, event_, component, visitorContext_);
    }
}

} // namespace kb::ecs

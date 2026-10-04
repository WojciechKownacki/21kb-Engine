#include "ecs/events/ComponentObserverContext.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <array>
#include <cstddef>
#include <vector>

namespace kb::ecs {

ComponentObserverContext::ComponentObserverContext(
    ComponentEventKind event,
    RawComponentObserverVisitor visitor,
    void* visitorContext,
    ComponentObserverContextFree visitorContextFree,
    std::size_t componentSize,
    WorldRegistrySet* observerRegistry,
    ComponentId componentId) noexcept
    : event_(event)
    , visitor_(visitor)
    , visitorContext_(visitorContext)
    , visitorContextFree_(visitorContextFree)
    , componentSize_(componentSize)
    , observerRegistry_(observerRegistry)
    , componentId_(componentId) {}

ComponentObserverContext::~ComponentObserverContext() {
    ReleaseObservation();
    if (visitorContextFree_ != nullptr) {
        visitorContextFree_(visitorContext_);
    }
}

void ComponentObserverContext::Dispatch(ecs_iter_t* iterator) {
    if (iterator == nullptr) {
        return;
    }

    auto* context = static_cast<ComponentObserverContext*>(iterator->ctx);
    if (context != nullptr) {
        ++context->activeDispatches_;
        try {
            context->DispatchRows(*iterator);
        } catch (...) {
            context->FinishDispatch();
            throw;
        }
        context->FinishDispatch();
    }
}

void ComponentObserverContext::Free(void* context) {
    auto* observer = static_cast<ComponentObserverContext*>(context);
    if (observer == nullptr) return;
    // Backend destruction, including raw ecs_delete and deferred deletion,
    // releases the subscription at its actual end of life. A callback can
    // destroy its own receiver; keep its visitor binding alive until it returns.
    observer->ReleaseObservation();
    if (observer->activeDispatches_ != 0U) {
        observer->freePending_ = true;
    } else {
        delete observer;
    }
}

void ComponentObserverContext::ReleaseObservation() noexcept {
    if (observerRegistry_ != nullptr) {
        observerRegistry_->ReleaseComponentObserver(componentId_);
        observerRegistry_ = nullptr;
    }
}

void ComponentObserverContext::FinishDispatch() noexcept {
    if (--activeDispatches_ == 0U && freePending_) {
        delete this;
    }
}

void ComponentObserverContext::DispatchRows(ecs_iter_t& iterator) const {
    if (visitor_ == nullptr || componentSize_ == 0 || iterator.count <= 0 || iterator.entities == nullptr) {
        return;
    }

    // Resolve the whole event batch before any visitor can destroy native
    // owners, register nested observers or release this observer's registry.
    std::array<Entity, 32U> inlineEntities{};
    std::vector<Entity> largeEntities;
    Entity* entities = inlineEntities.data();
    const std::size_t count = static_cast<std::size_t>(iterator.count);
    if (count > inlineEntities.size()) {
        largeEntities.resize(count);
        entities = largeEntities.data();
    }
    for (std::size_t row = 0U; row < count; ++row) {
        const Entity::IdType backendId = static_cast<Entity::IdType>(iterator.entities[row]);
        entities[row] = observerRegistry_ != nullptr
            ? observerRegistry_->ResolveObserverEntity(backendId)
            : Entity{ backendId };
    }

    const bool rowField = (iterator.row_fields & 1U) != 0U;
    const bool selfField = ecs_field_is_self(&iterator, 0);
    const auto* components = rowField ? nullptr : static_cast<const std::byte*>(ecs_field_w_size(&iterator, static_cast<ecs_size_t>(componentSize_), 0));
    for (int32_t row = 0; row < iterator.count && !freePending_; ++row) {
        const void* component = rowField ? ecs_field_at_w_size(&iterator, static_cast<ecs_size_t>(componentSize_), 0, row)
            : components == nullptr ? nullptr : components + (selfField ? static_cast<std::size_t>(row) * componentSize_ : 0U);
        visitor_(entities[static_cast<std::size_t>(row)], event_, component, visitorContext_);
    }
}

} // namespace kb::ecs

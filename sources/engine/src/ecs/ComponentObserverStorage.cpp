#include "ecs/events/ComponentObserverStorage.hpp"

#include "ecs/events/ComponentEventMapper.hpp"
#include "ecs/events/ComponentObserverContext.hpp"
#include "ecs/world/WorldRegistrySet.hpp"

#include <flecs.h>

#include <new>

namespace kb::ecs {
namespace {

void YieldExisting(ecs_world_t* world, ObserverId observer, ComponentId componentId, ComponentEventKind event) {
    const ecs_observer_t* backend = ecs_observer_get(world, observer);
    if (backend == nullptr) return;
    // Match the backend's yielding iterator, including a nontrivial query's
    // traversal and filter flags. Defer structural callback changes until the
    // iterator is exhausted, keeping its query and visitor context alive.
    ecs_defer_begin(world);
    const bool hasQuery = backend->query != nullptr;
    ecs_iter_t iterator = hasQuery ? ecs_query_iter(world, backend->query) : ecs_each_id(world, componentId);
    while (hasQuery ? ecs_query_next(&iterator) : ecs_each_next(&iterator)) {
        iterator.system = observer;
        iterator.event = ComponentEventMapper::ToFlecsEvent(event);
        iterator.event_id = componentId;
        iterator.ctx = backend->ctx;
        iterator.callback_ctx = backend->callback_ctx;
        iterator.run_ctx = backend->run_ctx;
        backend->callback(&iterator);
    }
    ecs_defer_end(world);
}

} // namespace

ObserverId ComponentObserverStorage::Create(
    ecs_world_t* world,
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentObserverVisitor visitor,
    void* context,
    ComponentObserverContextFree contextFree,
    bool yieldExisting,
    WorldRegistrySet* observerRegistry) noexcept {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        if (observerRegistry != nullptr) observerRegistry->ReleaseComponentObserver(componentId);
        if (contextFree != nullptr) {
            contextFree(context);
        }
        return 0;
    }

    auto* observerContext = new (std::nothrow) ComponentObserverContext{ event, visitor, context, contextFree, componentSize, observerRegistry, componentId };
    if (observerContext == nullptr) {
        if (observerRegistry != nullptr) observerRegistry->ReleaseComponentObserver(componentId);
        if (contextFree != nullptr) {
            contextFree(context);
        }
        return 0;
    }

    ecs_observer_desc_t desc{};
    try {
        if (observerRegistry != nullptr) desc.entity = observerRegistry->CreateObserverEntity(world);
    } catch (...) {
        ComponentObserverContext::Free(observerContext);
        observerContext->FinishDispatch();
        return 0U;
    }
    desc.query.terms[0].id = componentId;
    desc.events[0] = ComponentEventMapper::ToFlecsEvent(event);
    desc.callback = &ComponentObserverContext::Dispatch;
    desc.ctx = observerContext;
    desc.ctx_free = &ComponentObserverContext::Free;
    // Backend init binds its observer object after synchronous yieldExisting.
    // Reentrant registration can move that binding before init finishes. Yield
    // only once the fully initialized observer is attached to its entity.
    desc.yield_existing = false;

    const ObserverId observer = static_cast<ObserverId>(ecs_observer_init(world, &desc));
    if (observer == 0) {
        if (desc.entity != 0U && ecs_is_alive(world, desc.entity)) ecs_delete(world, desc.entity);
        ComponentObserverContext::Free(observerContext);
    }
    if (observer != 0U && yieldExisting && event != ComponentEventKind::Removed) {
        YieldExisting(world, observer, componentId, event);
    }
    observerContext->FinishDispatch(); // Release after every yielding callback returned.
    return observer;
}

void ComponentObserverStorage::Destroy(ecs_world_t* world, ObserverId observer) noexcept {
    if (world != nullptr && observer != 0 && ecs_is_alive(world, observer)) {
        ecs_delete(world, observer);
    }
}

} // namespace kb::ecs

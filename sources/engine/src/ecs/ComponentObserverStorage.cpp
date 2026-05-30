#include "ecs/events/ComponentObserverStorage.hpp"

#include "ecs/events/ComponentEventMapper.hpp"
#include "ecs/events/ComponentObserverContext.hpp"

#include <flecs.h>

#include <new>

namespace kb::ecs {

ObserverId ComponentObserverStorage::Create(
    ecs_world_t* world,
    ComponentId componentId,
    std::size_t componentSize,
    ComponentEventKind event,
    RawComponentObserverVisitor visitor,
    void* context,
    ComponentObserverContextFree contextFree,
    bool yieldExisting) noexcept {
    if (world == nullptr || componentId == 0 || componentSize == 0 || visitor == nullptr) {
        if (contextFree != nullptr) {
            contextFree(context);
        }
        return 0;
    }

    auto* observerContext = new (std::nothrow) ComponentObserverContext{ event, visitor, context, contextFree, componentSize };
    if (observerContext == nullptr) {
        if (contextFree != nullptr) {
            contextFree(context);
        }
        return 0;
    }

    ecs_observer_desc_t desc{};
    desc.query.terms[0].id = componentId;
    desc.events[0] = ComponentEventMapper::ToFlecsEvent(event);
    desc.callback = &ComponentObserverContext::Dispatch;
    desc.ctx = observerContext;
    desc.ctx_free = &ComponentObserverContext::Free;
    desc.yield_existing = yieldExisting && event != ComponentEventKind::Removed;

    const ObserverId observer = static_cast<ObserverId>(ecs_observer_init(world, &desc));
    if (observer == 0) {
        ComponentObserverContext::Free(observerContext);
    }
    return observer;
}

void ComponentObserverStorage::Destroy(ecs_world_t* world, ObserverId observer) noexcept {
    if (world != nullptr && observer != 0 && ecs_is_alive(world, observer)) {
        ecs_delete(world, observer);
    }
}

} // namespace kb::ecs

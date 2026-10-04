#pragma once

#include "ecs/events/ComponentObserverTypes.hpp"
#include "engine/ecs/ComponentId.hpp"

#include <cstddef>

struct ecs_iter_t;

namespace kb::ecs {

class WorldRegistrySet;

class ComponentObserverContext {
public:
    ComponentObserverContext(
        ComponentEventKind event,
        RawComponentObserverVisitor visitor,
        void* visitorContext,
        ComponentObserverContextFree visitorContextFree,
        std::size_t componentSize,
        WorldRegistrySet* observerRegistry,
        ComponentId componentId) noexcept;

    ~ComponentObserverContext();

    ComponentObserverContext(const ComponentObserverContext&) = delete;
    ComponentObserverContext& operator=(const ComponentObserverContext&) = delete;

    static void Dispatch(ecs_iter_t* iterator);
    static void Free(void* context);

private:
    friend class ComponentObserverStorage;

    void DispatchRows(ecs_iter_t& iterator) const;
    void ReleaseObservation() noexcept;
    void FinishDispatch() noexcept;

    ComponentEventKind event_ = ComponentEventKind::Added;
    RawComponentObserverVisitor visitor_ = nullptr;
    void* visitorContext_ = nullptr;
    ComponentObserverContextFree visitorContextFree_ = nullptr;
    std::size_t componentSize_ = 0;
    WorldRegistrySet* observerRegistry_ = nullptr;
    ComponentId componentId_ = 0U;
    // The creator retains one use until ecs_observer_init returns; callbacks
    // retain further uses. This also balances a backend failure calling Free.
    std::size_t activeDispatches_ = 1U;
    bool freePending_ = false;
};

} // namespace kb::ecs

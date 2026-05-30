#pragma once

#include "ecs/events/ComponentObserverTypes.hpp"

#include <cstddef>

struct ecs_iter_t;

namespace kb::ecs {

class ComponentObserverContext {
public:
    ComponentObserverContext(
        ComponentEventKind event,
        RawComponentObserverVisitor visitor,
        void* visitorContext,
        ComponentObserverContextFree visitorContextFree,
        std::size_t componentSize) noexcept;

    ~ComponentObserverContext();

    ComponentObserverContext(const ComponentObserverContext&) = delete;
    ComponentObserverContext& operator=(const ComponentObserverContext&) = delete;

    static void Dispatch(ecs_iter_t* iterator);
    static void Free(void* context);

private:
    void DispatchRows(ecs_iter_t& iterator) const;

    ComponentEventKind event_ = ComponentEventKind::Added;
    RawComponentObserverVisitor visitor_ = nullptr;
    void* visitorContext_ = nullptr;
    ComponentObserverContextFree visitorContextFree_ = nullptr;
    std::size_t componentSize_ = 0;
};

} // namespace kb::ecs

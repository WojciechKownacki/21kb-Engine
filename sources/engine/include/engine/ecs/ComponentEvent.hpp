#pragma once

#include "engine/ecs/Entity.hpp"

namespace kb::ecs {

enum class ComponentEventKind {
    Added,
    Removed,
    Modified
};

template <typename T>
using ComponentEventVisitor = void (*)(Entity entity, ComponentEventKind event, const T* component, void* context);

} // namespace kb::ecs

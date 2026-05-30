#pragma once

#include "engine/ecs/ComponentEvent.hpp"

namespace kb::ecs {

using RawComponentObserverVisitor = void (*)(Entity entity, ComponentEventKind event, const void* component, void* context);
using ComponentObserverContextFree = void (*)(void* context);

} // namespace kb::ecs

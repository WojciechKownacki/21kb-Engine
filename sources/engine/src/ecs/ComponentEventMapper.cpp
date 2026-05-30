#include "ecs/events/ComponentEventMapper.hpp"

#include <flecs.h>

namespace kb::ecs {

Entity::IdType ComponentEventMapper::ToFlecsEvent(ComponentEventKind event) noexcept {
    switch (event) {
    case ComponentEventKind::Added:
        return static_cast<Entity::IdType>(EcsOnAdd);
    case ComponentEventKind::Removed:
        return static_cast<Entity::IdType>(EcsOnRemove);
    case ComponentEventKind::Modified:
        return static_cast<Entity::IdType>(EcsOnSet);
    }
    return static_cast<Entity::IdType>(EcsOnSet);
}

} // namespace kb::ecs

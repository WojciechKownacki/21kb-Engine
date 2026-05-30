#pragma once

#include "engine/ecs/ComponentEvent.hpp"
#include "engine/ecs/Entity.hpp"

namespace kb::ecs {

class ComponentEventMapper {
public:
    [[nodiscard]] static Entity::IdType ToFlecsEvent(ComponentEventKind event) noexcept;
};

} // namespace kb::ecs

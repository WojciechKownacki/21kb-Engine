#pragma once

#include "engine/ecs/ComponentSerialization.hpp"
#include "engine/ecs/Entity.hpp"

#include <string>
#include <vector>

namespace kb::ecs {

struct EditorEntityInspection {
    Entity entity;
    std::string name;
    Entity parent;
    std::vector<SerializedComponent> components;
};

struct EditorWorldInspection {
    std::vector<EditorEntityInspection> entities;
};

} // namespace kb::ecs

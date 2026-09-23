#pragma once

#include "engine/ecs/ComponentTypeInfo.hpp"
#include "engine/ecs/WorldEditorInspection.hpp"

#include <span>

namespace kb::ecs {

class World;

class EditorEntityInspectionBuilder {
public:
    [[nodiscard]] static bool Build(
        const World& world,
        Entity entity,
        std::span<const ComponentTypeInfo> componentTypes,
        EditorEntityInspection& output);
};

} // namespace kb::ecs

#pragma once

#include "engine/ecs/WorldEditorInspection.hpp"

namespace kb::ecs {

class World;

class EditorEntityInspectionBuilder {
public:
    [[nodiscard]] static bool Build(const World& world, Entity entity, EditorEntityInspection& output);
};

} // namespace kb::ecs

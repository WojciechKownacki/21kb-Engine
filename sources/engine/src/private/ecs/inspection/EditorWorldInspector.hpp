#pragma once

#include "engine/ecs/WorldEditorInspection.hpp"

namespace kb::ecs {

class World;

class EditorWorldInspector {
public:
    [[nodiscard]] static bool Inspect(const World& world, EditorWorldInspection& output);
};

} // namespace kb::ecs

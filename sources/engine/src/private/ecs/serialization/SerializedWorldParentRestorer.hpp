#pragma once

#include "engine/ecs/WorldSerialization.hpp"

namespace kb::ecs {

class SerializedWorldRestoreMap;
class World;

class SerializedWorldParentRestorer {
public:
    [[nodiscard]] static bool RestoreParents(World& world, const SerializedWorld& source, const SerializedWorldRestoreMap& restoredEntities);
};

} // namespace kb::ecs

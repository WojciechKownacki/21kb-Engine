#pragma once

#include "engine/ecs/WorldSerialization.hpp"

namespace kb::ecs {

class SerializedWorldRestoreMap;
class World;

class SerializedWorldComponentRestorer {
public:
    [[nodiscard]] static bool RestoreComponents(World& world, const SerializedWorld& source, const SerializedWorldRestoreMap& restoredEntities);
};

} // namespace kb::ecs

#pragma once

#include "engine/ecs/WorldSerialization.hpp"

namespace kb::ecs {

class SerializedWorldRestoreMap;
class World;

class SerializedWorldEntityFactory {
public:
    static void CreateEntities(World& world, const SerializedWorld& source, SerializedWorldRestoreMap& restoredEntities);
};

} // namespace kb::ecs

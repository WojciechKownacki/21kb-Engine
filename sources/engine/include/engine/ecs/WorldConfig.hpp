#pragma once

#include "engine/ecs/ChunkSizeProfile.hpp"
#include "engine/ecs/QueryExecutionSettings.hpp"

#include <cstddef>

namespace kb::ecs {

struct WorldConfig {
    ChunkSizeProfile chunkSizeProfile = kDefaultChunkSizeProfile;
    std::size_t reserveEntities = 0;
    std::size_t reserveArchetypes = 0;
    std::size_t reserveQueryCache = 0;
    std::size_t executionGrainSize = kDefaultQueryExecutionGrainSize;
};

} // namespace kb::ecs

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
    std::size_t maxNativeStorageCommittedPayloadBytes = 0;
    std::size_t executionGrainSize = kDefaultQueryExecutionGrainSize;
    std::size_t queryPrefetchDistance = 0;
    std::size_t workerThreadLimit = 0;
    bool adaptiveQueryExecution = true;
    bool mirrorEntitiesToBackend = true;
    bool mirrorNativeComponentChangesToBackend = true;
    // With mirroring on, in-place value writes to existing native components are published to the backend world only
    // for components that have a World::ObserveComponent observer (the backend copy is read by nothing else).
    // Set this to false to publish every write, e.g. when raw backend observers are registered through NativeHandle().
    bool mirrorValueWritesOnlyForObservedComponents = true;
    bool trackEntityCatalog = true;
};

} // namespace kb::ecs

#pragma once

#include <cstddef>

namespace kb::ecs {

class WorkerPool;

inline constexpr std::size_t kDefaultQueryExecutionGrainSize = 256;

enum class QueryIterationOrder {
    StorageOrder,
    Deterministic,
    ChunkOrder,
};

struct QueryExecutionSettings {
    std::size_t maxBatchSize = 0;
    QueryIterationOrder iterationOrder = QueryIterationOrder::StorageOrder;
    std::size_t prefetchDistance = 0;
    WorkerPool* workerPool = nullptr;
};

} // namespace kb::ecs

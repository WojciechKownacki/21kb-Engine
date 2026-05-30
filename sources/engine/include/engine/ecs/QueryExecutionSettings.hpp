#pragma once

#include <cstddef>

namespace kb::ecs {

inline constexpr std::size_t kDefaultQueryExecutionGrainSize = 256;

struct QueryExecutionSettings {
    std::size_t maxBatchSize = 0;
};

} // namespace kb::ecs

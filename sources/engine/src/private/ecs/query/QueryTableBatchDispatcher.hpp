#pragma once

#include "ecs/query/QueryLimits.hpp"
#include "engine/ecs/Query.hpp"

#include <cstddef>
#include <span>

struct ecs_iter_t;

namespace kb::ecs {

class QueryTableBatchDispatcher {
public:
    static void Dispatch(
        const ecs_iter_t& iterator,
        std::span<const std::size_t> componentSizes,
        const QueryComponentPointerBlock& fieldComponents,
        std::size_t maxBatchSize,
        QueryRawBatchVisitor visitor,
        void* context);
};

} // namespace kb::ecs

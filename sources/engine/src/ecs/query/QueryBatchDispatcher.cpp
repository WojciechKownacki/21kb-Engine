#include "ecs/query/QueryBatchDispatcher.hpp"

#include "ecs/query/QueryFieldReader.hpp"
#include "ecs/query/QueryLimits.hpp"
#include "ecs/query/QueryTableBatchDispatcher.hpp"

#include <flecs.h>

namespace kb::ecs {
namespace {

[[nodiscard]] std::size_t ResolveBatchSize(QueryExecutionSettings settings, std::size_t defaultExecutionGrainSize) noexcept {
    return settings.maxBatchSize == 0 ? defaultExecutionGrainSize : settings.maxBatchSize;
}

} // namespace

void QueryBatchDispatcher::Execute(
    ecs_world_t* world,
    ecs_query_t* query,
    std::span<const std::size_t> componentSizes,
    std::size_t defaultExecutionGrainSize,
    QueryExecutionSettings settings,
    QueryRawBatchVisitor visitor,
    void* context) {
    if (world == nullptr || query == nullptr || componentSizes.empty() || visitor == nullptr) {
        return;
    }

    const std::size_t maxBatchSize = ResolveBatchSize(settings, defaultExecutionGrainSize);
    QueryComponentPointerBlock fieldComponents{};
    ecs_iter_t iterator = ecs_query_iter(world, query);
    while (ecs_query_next(&iterator)) {
        if (QueryFieldReader::Read(iterator, componentSizes, fieldComponents)) {
            QueryTableBatchDispatcher::Dispatch(iterator, componentSizes, fieldComponents, maxBatchSize, visitor, context);
        }
    }
}

} // namespace kb::ecs

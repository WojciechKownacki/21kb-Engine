#include "ecs/QueryState.hpp"

#include "ecs/query/QueryBatchDispatcher.hpp"
#include "ecs/query/QueryDescriptorBuilder.hpp"
#include "ecs/query/QueryRowDispatcher.hpp"

#include <flecs.h>

namespace kb::ecs {

QueryState::QueryState(ecs_world_t* world, std::span<const ComponentId> componentIds, std::span<const std::size_t> componentSizes, std::size_t defaultExecutionGrainSize)
    : world_(world)
    , defaultExecutionGrainSize_(defaultExecutionGrainSize == 0 ? kDefaultQueryExecutionGrainSize : defaultExecutionGrainSize)
    , componentSizes_(componentSizes.begin(), componentSizes.end()) {
    query_ = QueryDescriptorBuilder::Build(world_, componentIds, componentSizes);
}

bool QueryState::IsValid() const noexcept {
    return world_ != nullptr && query_ && !componentSizes_.empty();
}

void QueryState::ForEach(QueryRawVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryRowDispatcher::Execute(*this, visitor, context);
}

void QueryState::ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryBatchDispatcher::Execute(world_, query_.Get(), componentSizes_, defaultExecutionGrainSize_, settings, visitor, context);
}

void DestroyQueryState(QueryState* state) noexcept {
    delete state;
}

bool IsQueryStateValid(const QueryState* state) noexcept {
    return state != nullptr && state->IsValid();
}

void ForEachQueryState(const QueryState* state, QueryRawVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEach(visitor, context);
    }
}

void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEachBatch(settings, visitor, context);
    }
}

} // namespace kb::ecs

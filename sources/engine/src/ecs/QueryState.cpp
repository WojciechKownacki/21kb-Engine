#include "ecs/QueryState.hpp"

#include "ecs/query/QueryBatchDispatcher.hpp"
#include "ecs/query/QueryPlan.hpp"
#include "ecs/query/QueryRowDispatcher.hpp"

#include <flecs.h>

#include <utility>

namespace kb::ecs {

QueryState::QueryState(ecs_world_t* world, std::shared_ptr<QueryPlan> plan, std::size_t defaultExecutionGrainSize)
    : world_(world)
    , plan_(std::move(plan))
    , defaultExecutionGrainSize_(defaultExecutionGrainSize == 0 ? kDefaultQueryExecutionGrainSize : defaultExecutionGrainSize) {}

bool QueryState::IsValid() const noexcept {
    return world_ != nullptr && plan_ && plan_->IsValid();
}

std::span<const ComponentId> QueryState::ComponentIds() const noexcept {
    if (!plan_) {
        return {};
    }
    return plan_->ComponentIds();
}

std::span<const std::size_t> QueryState::ComponentSizes() const noexcept {
    if (!plan_) {
        return {};
    }
    return plan_->ComponentSizes();
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

    QueryBatchDispatcher::Execute(
        world_,
        plan_->Native(),
        plan_->ComponentSizes(),
        plan_->HasChangeFilters(),
        defaultExecutionGrainSize_,
        settings,
        visitor,
        context);
}

void QueryState::ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) const {
    if (!IsValid() || visitor == nullptr) {
        return;
    }

    QueryBatchDispatcher::ExecuteMutable(
        world_,
        plan_->Native(),
        plan_->ComponentIds(),
        plan_->ComponentSizes(),
        plan_->HasChangeFilters(),
        defaultExecutionGrainSize_,
        settings,
        visitor,
        context);
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

void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) {
    if (state != nullptr) {
        state->ForEachMutableBatch(settings, visitor, context);
    }
}

} // namespace kb::ecs

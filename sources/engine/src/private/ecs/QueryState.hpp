#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/Query.hpp"

#include <cstddef>
#include <memory>
#include <span>

struct ecs_world_t;

namespace kb::ecs {

class QueryPlan;

class QueryState {
public:
    QueryState(ecs_world_t* world, std::shared_ptr<QueryPlan> plan, std::size_t defaultExecutionGrainSize);
    ~QueryState() = default;

    QueryState(const QueryState&) = delete;
    QueryState& operator=(const QueryState&) = delete;
    QueryState(QueryState&&) = delete;
    QueryState& operator=(QueryState&&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::span<const std::size_t> ComponentSizes() const noexcept;
    void ForEach(QueryRawVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) const;
    void ForEachMutableBatch(QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context) const;

private:
    ecs_world_t* world_ = nullptr;
    std::shared_ptr<QueryPlan> plan_;
    std::size_t defaultExecutionGrainSize_ = kDefaultQueryExecutionGrainSize;
};

} // namespace kb::ecs

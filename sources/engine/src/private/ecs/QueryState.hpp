#pragma once

#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/Entity.hpp"
#include "engine/ecs/Query.hpp"
#include "ecs/query/FlecsQueryHandle.hpp"

#include <cstddef>
#include <span>
#include <vector>

struct ecs_world_t;

namespace kb::ecs {

class QueryState {
public:
    QueryState(ecs_world_t* world, std::span<const ComponentId> componentIds, std::span<const std::size_t> componentSizes, std::size_t defaultExecutionGrainSize);
    ~QueryState() = default;

    QueryState(const QueryState&) = delete;
    QueryState& operator=(const QueryState&) = delete;
    QueryState(QueryState&&) = delete;
    QueryState& operator=(QueryState&&) = delete;

    [[nodiscard]] bool IsValid() const noexcept;
    void ForEach(QueryRawVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context) const;

private:
    ecs_world_t* world_ = nullptr;
    FlecsQueryHandle query_;
    std::size_t defaultExecutionGrainSize_ = kDefaultQueryExecutionGrainSize;
    std::vector<std::size_t> componentSizes_;
};

} // namespace kb::ecs

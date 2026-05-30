#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryBatch.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace kb::ecs {

class QueryState;

using QueryRawVisitor = void (*)(Entity entity, const void* const* components, void* context);
using QueryRawBatchVisitor = void (*)(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* context);

void DestroyQueryState(QueryState* state) noexcept;
[[nodiscard]] bool IsQueryStateValid(const QueryState* state) noexcept;
void ForEachQueryState(const QueryState* state, QueryRawVisitor visitor, void* context);
void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context);

template <typename... Components>
class Query {
public:
    using Visitor = void (*)(Entity entity, const Components&... components, void* context);
    using Batch = QueryBatch<Components...>;
    using BatchVisitor = void (*)(const Batch& batch, void* context);

    Query() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept;
    void ForEach(Visitor visitor, void* context) const;
    void ForEachBatch(BatchVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, BatchVisitor visitor, void* context) const;

private:
    friend class World;

    using StatePtr = std::unique_ptr<QueryState, void (*)(QueryState*) noexcept>;

    struct AdapterContext {
        Visitor visitor = nullptr;
        BatchVisitor batchVisitor = nullptr;
        void* context = nullptr;
    };

    explicit Query(QueryState* state) noexcept;

    template <std::size_t... Indices>
    static void Visit(Entity entity, const void* const* components, void* adapter, std::index_sequence<Indices...>);

    static void Visit(Entity entity, const void* const* components, void* adapter);

    template <std::size_t... Indices>
    static void VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter, std::index_sequence<Indices...>);

    static void VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter);

    StatePtr state_{ nullptr, &DestroyQueryState };
};

} // namespace kb::ecs

#include "engine/ecs/Query.inl"

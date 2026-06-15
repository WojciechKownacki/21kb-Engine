#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/QueryBatch.hpp"

#include <cstddef>
#include <memory>
#include <utility>

namespace kb::ecs {

class QueryState;
class QueryBatchExecutionScratch;

using QueryRawVisitor = void (*)(Entity entity, const void* const* components, void* context);
using QueryRawBatchVisitor = void (*)(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* context);
using QueryRawMutableBatchVisitor = void (*)(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* context);

void DestroyQueryState(QueryState* state) noexcept;
[[nodiscard]] bool IsQueryStateValid(const QueryState* state) noexcept;
void ForEachQueryState(const QueryState* state, QueryRawVisitor visitor, void* context);
void PrepareQueryStateBatchExecution(const QueryState* state, QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch);
void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context);
void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch);
void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context);
void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch);

template <typename... Components>
class Query {
public:
    using Visitor = void (*)(Entity entity, const Components&... components, void* context);
    using Batch = QueryBatch<Components...>;
    using MutableBatch = MutableQueryBatch<Components...>;
    using BatchVisitor = void (*)(const Batch& batch, void* context);
    using MutableBatchVisitor = void (*)(MutableBatch& batch, void* context);

    Query() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept;
    void PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    void ForEach(Visitor visitor, void* context) const;
    void ForEachBatch(BatchVisitor visitor, void* context) const;
    void ForEachBatch(QueryExecutionSettings settings, BatchVisitor visitor, void* context) const;
    void ForEachMutableBatch(MutableBatchVisitor visitor, void* context) const;
    void ForEachMutableBatch(QueryExecutionSettings settings, MutableBatchVisitor visitor, void* context) const;
    template <typename Kernel>
    void ForEachBatchKernel(Kernel&& kernel) const;
    template <typename Kernel>
    void ForEachBatchKernel(QueryExecutionSettings settings, Kernel&& kernel) const;
    template <typename Kernel>
    void ForEachMutableBatchKernel(Kernel&& kernel) const;
    template <typename Kernel>
    void ForEachMutableBatchKernel(QueryExecutionSettings settings, Kernel&& kernel) const;
    template <typename Kernel>
    void ForEachMutableBatchKernel(QueryExecutionSettings settings, Kernel&& kernel, QueryBatchExecutionScratch& scratch) const;

private:
    friend class World;

    using StatePtr = std::unique_ptr<QueryState, void (*)(QueryState*) noexcept>;

    struct AdapterContext {
        Visitor visitor = nullptr;
        BatchVisitor batchVisitor = nullptr;
        MutableBatchVisitor mutableBatchVisitor = nullptr;
        void* context = nullptr;
    };

    explicit Query(QueryState* state) noexcept;

    template <std::size_t... Indices>
    static void Visit(Entity entity, const void* const* components, void* adapter, std::index_sequence<Indices...>);

    static void Visit(Entity entity, const void* const* components, void* adapter);

    template <std::size_t... Indices>
    static void VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter, std::index_sequence<Indices...>);

    static void VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter);

    template <typename Kernel, std::size_t... Indices>
    static void VisitBatchKernel(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* kernel, std::index_sequence<Indices...>);

    template <typename Kernel>
    static void VisitBatchKernel(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* kernel);

    template <std::size_t... Indices>
    static void VisitMutableBatch(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* adapter, std::index_sequence<Indices...>);

    static void VisitMutableBatch(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* adapter);

    template <typename Kernel, std::size_t... Indices>
    static void VisitMutableBatchKernel(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* kernel, std::index_sequence<Indices...>);

    template <typename Kernel>
    static void VisitMutableBatchKernel(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* kernel);

    StatePtr state_{ nullptr, &DestroyQueryState };
};

} // namespace kb::ecs

#include "engine/ecs/Query.inl"

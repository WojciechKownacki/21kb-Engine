#pragma once

#include "engine/ecs/Entity.hpp"
#include "engine/ecs/ComponentId.hpp"
#include "engine/ecs/QueryBatch.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace kb::ecs {

class QueryState;
class QueryBatchExecutionScratch;

using QueryRawVisitor = void (*)(Entity entity, const void* const* components, void* context);
using QueryRawBatchVisitor = void (*)(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* context);
using QueryRawMutableBatchVisitor = void (*)(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* context);

void DestroyQueryState(QueryState* state) noexcept;
[[nodiscard]] bool IsQueryStateValid(const QueryState* state) noexcept;
[[nodiscard]] std::span<const ComponentId> QueryStateComponentIds(const QueryState* state) noexcept;
[[nodiscard]] std::uint64_t QueryStateStructuralVersion(const QueryState* state) noexcept;
void ForEachQueryState(const QueryState* state, QueryRawVisitor visitor, void* context);
void PrepareQueryStateBatchExecution(const QueryState* state, QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch);
void PrepareQueryStateMutableBatchExecution(const QueryState* state, QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch);
void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context);
void ForEachQueryStateBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch);
void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context);
void ForEachQueryStateMutableBatch(const QueryState* state, QueryExecutionSettings settings, QueryRawMutableBatchVisitor visitor, void* context, QueryBatchExecutionScratch& scratch);

template <typename... Components>
class Query {
public:
    // Create separate Query instances before concurrent read-only execution. One instance
    // cannot be invoked concurrently because its record and change-filter caches are mutable.
    // Keep World structure stable while queries run; CreateQuery is not synchronized.
    // Mutable queries must not overlap other queries: they publish shared storage metadata.
    // Read-only query telemetry is synchronized across separate instances.
    // Queries remain usable after moving their World; the destination World must outlive them.
    using Visitor = void (*)(Entity entity, const Components&... components, void* context);
    using Batch = QueryBatch<Components...>;
    using MutableBatch = MutableQueryBatch<Components...>;
    using BatchVisitor = void (*)(const Batch& batch, void* context);
    using MutableBatchVisitor = void (*)(MutableBatch& batch, void* context);

    Query() noexcept = default;

    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] std::span<const ComponentId> ComponentIds() const noexcept;
    [[nodiscard]] std::uint64_t StructuralVersion() const noexcept;
    void PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
    void PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const;
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
    void ForEachBatchKernel(QueryExecutionSettings settings, Kernel&& kernel, QueryBatchExecutionScratch& scratch) const;
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

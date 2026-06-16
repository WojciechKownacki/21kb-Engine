#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace kb::ecs {

template <typename... Components>
void Query<Components...>::PrepareBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid()) {
        return;
    }

    PrepareQueryStateBatchExecution(state_.get(), settings, scratch);
}

template <typename... Components>
void Query<Components...>::PrepareMutableBatchExecution(QueryExecutionSettings settings, QueryBatchExecutionScratch& scratch) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid()) {
        return;
    }

    PrepareQueryStateMutableBatchExecution(state_.get(), settings, scratch);
}

template <typename... Components>
void Query<Components...>::ForEach(Visitor visitor, void* context) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid() || visitor == nullptr) {
        return;
    }

    AdapterContext adapter{
        .visitor = visitor,
        .context = context,
    };
    ForEachQueryState(state_.get(), &Visit, &adapter);
}

template <typename... Components>
void Query<Components...>::ForEachBatch(BatchVisitor visitor, void* context) const {
    ForEachBatch(QueryExecutionSettings{}, visitor, context);
}

template <typename... Components>
void Query<Components...>::ForEachBatch(QueryExecutionSettings settings, BatchVisitor visitor, void* context) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid() || visitor == nullptr) {
        return;
    }

    AdapterContext adapter{
        .batchVisitor = visitor,
        .context = context,
    };
    ForEachQueryStateBatch(state_.get(), settings, &VisitBatch, &adapter);
}

template <typename... Components>
void Query<Components...>::ForEachMutableBatch(MutableBatchVisitor visitor, void* context) const {
    ForEachMutableBatch(QueryExecutionSettings{}, visitor, context);
}

template <typename... Components>
void Query<Components...>::ForEachMutableBatch(QueryExecutionSettings settings, MutableBatchVisitor visitor, void* context) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid() || visitor == nullptr) {
        return;
    }

    AdapterContext adapter{
        .mutableBatchVisitor = visitor,
        .context = context,
    };
    ForEachQueryStateMutableBatch(state_.get(), settings, &VisitMutableBatch, &adapter);
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::ForEachBatchKernel(Kernel&& kernel) const {
    ForEachBatchKernel(QueryExecutionSettings{}, std::forward<Kernel>(kernel));
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::ForEachBatchKernel(QueryExecutionSettings settings, Kernel&& kernel) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid()) {
        return;
    }

    using KernelType = std::remove_reference_t<Kernel>;
    ForEachQueryStateBatch(state_.get(), settings, &VisitBatchKernel<KernelType>, std::addressof(kernel));
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::ForEachMutableBatchKernel(Kernel&& kernel) const {
    ForEachMutableBatchKernel(QueryExecutionSettings{}, std::forward<Kernel>(kernel));
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::ForEachMutableBatchKernel(QueryExecutionSettings settings, Kernel&& kernel) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid()) {
        return;
    }

    using KernelType = std::remove_reference_t<Kernel>;
    ForEachQueryStateMutableBatch(state_.get(), settings, &VisitMutableBatchKernel<KernelType>, std::addressof(kernel));
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::ForEachMutableBatchKernel(QueryExecutionSettings settings, Kernel&& kernel, QueryBatchExecutionScratch& scratch) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");
    static_assert((std::is_trivially_destructible_v<Components> && ...), "ECS query components must be trivially destructible");

    if (!IsValid()) {
        return;
    }

    using KernelType = std::remove_reference_t<Kernel>;
    ForEachQueryStateMutableBatch(state_.get(), settings, &VisitMutableBatchKernel<KernelType>, std::addressof(kernel), scratch);
}

} // namespace kb::ecs

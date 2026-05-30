#pragma once

#include <type_traits>

namespace kb::ecs {

template <typename... Components>
void Query<Components...>::ForEach(Visitor visitor, void* context) const {
    static_assert(sizeof...(Components) > 0, "ECS query must have at least one component");
    static_assert((std::is_trivially_copyable_v<Components> && ...), "ECS query components must be trivially copyable");

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

    if (!IsValid() || visitor == nullptr) {
        return;
    }

    AdapterContext adapter{
        .batchVisitor = visitor,
        .context = context,
    };
    ForEachQueryStateBatch(state_.get(), settings, &VisitBatch, &adapter);
}

} // namespace kb::ecs

#pragma once

#include <utility>

namespace kb::ecs {

template <typename... Components>
template <std::size_t... Indices>
void Query<Components...>::Visit(Entity entity, const void* const* components, void* adapter, std::index_sequence<Indices...>) {
    auto* typedAdapter = static_cast<AdapterContext*>(adapter);
    typedAdapter->visitor(entity, *static_cast<const Components*>(components[Indices])..., typedAdapter->context);
}

template <typename... Components>
void Query<Components...>::Visit(Entity entity, const void* const* components, void* adapter) {
    Visit(entity, components, adapter, std::index_sequence_for<Components...>{});
}

template <typename... Components>
template <std::size_t... Indices>
void Query<Components...>::VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter, std::index_sequence<Indices...>) {
    auto* typedAdapter = static_cast<AdapterContext*>(adapter);
    const Batch batch{
        entityIds,
        count,
        typename Batch::ComponentPointers{ static_cast<const Components*>(components[Indices])... },
    };
    typedAdapter->batchVisitor(batch, typedAdapter->context);
}

template <typename... Components>
void Query<Components...>::VisitBatch(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* adapter) {
    VisitBatch(entityIds, count, components, adapter, std::index_sequence_for<Components...>{});
}

template <typename... Components>
template <typename Kernel, std::size_t... Indices>
void Query<Components...>::VisitBatchKernel(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* kernel, std::index_sequence<Indices...>) {
    const Batch batch{
        entityIds,
        count,
        typename Batch::ComponentPointers{ static_cast<const Components*>(components[Indices])... },
    };
    (*static_cast<Kernel*>(kernel))(batch);
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::VisitBatchKernel(const Entity::IdType* entityIds, std::size_t count, const void* const* components, void* kernel) {
    VisitBatchKernel<Kernel>(entityIds, count, components, kernel, std::index_sequence_for<Components...>{});
}

template <typename... Components>
template <std::size_t... Indices>
void Query<Components...>::VisitMutableBatch(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* adapter, std::index_sequence<Indices...>) {
    auto* typedAdapter = static_cast<AdapterContext*>(adapter);
    MutableBatch batch{
        entityIds,
        count,
        typename MutableBatch::ComponentPointers{ static_cast<Components*>(components[Indices])... },
    };
    typedAdapter->mutableBatchVisitor(batch, typedAdapter->context);
}

template <typename... Components>
void Query<Components...>::VisitMutableBatch(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* adapter) {
    VisitMutableBatch(entityIds, count, components, adapter, std::index_sequence_for<Components...>{});
}

template <typename... Components>
template <typename Kernel, std::size_t... Indices>
void Query<Components...>::VisitMutableBatchKernel(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* kernel, std::index_sequence<Indices...>) {
    MutableBatch batch{
        entityIds,
        count,
        typename MutableBatch::ComponentPointers{ static_cast<Components*>(components[Indices])... },
    };
    (*static_cast<Kernel*>(kernel))(batch);
}

template <typename... Components>
template <typename Kernel>
void Query<Components...>::VisitMutableBatchKernel(const Entity::IdType* entityIds, std::size_t count, void* const* components, void* kernel) {
    VisitMutableBatchKernel<Kernel>(entityIds, count, components, kernel, std::index_sequence_for<Components...>{});
}

} // namespace kb::ecs

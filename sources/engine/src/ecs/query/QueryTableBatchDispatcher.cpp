#include "ecs/query/QueryTableBatchDispatcher.hpp"

#include "engine/ecs/QueryPrefetch.hpp"

#include <flecs.h>

#include <algorithm>
#include <cstdint>

namespace kb::ecs {
namespace {

void PrefetchReadOnlyBatch(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::span<const std::size_t> componentSizes,
    const QueryComponentPointerBlock& fieldComponents,
    std::size_t offset,
    std::size_t prefetchDistance) noexcept {
    if (prefetchDistance == 0) {
        return;
    }

    if (prefetchDistance >= entityCount - offset) {
        return;
    }

    const std::size_t prefetchOffset = offset + prefetchDistance;
    PrefetchQueryMemory(entityIds + prefetchOffset);
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        const auto* bytes = static_cast<const std::uint8_t*>(fieldComponents[field]);
        PrefetchQueryMemory(bytes + prefetchOffset * componentSizes[field]);
    }
}

void PrefetchReadOnlySingleComponent(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    const std::uint8_t* componentBytes,
    std::size_t componentSize,
    std::size_t offset,
    std::size_t prefetchDistance) noexcept {
    if (prefetchDistance == 0 || prefetchDistance >= entityCount - offset) {
        return;
    }

    const std::size_t prefetchOffset = offset + prefetchDistance;
    PrefetchQueryMemory(entityIds + prefetchOffset);
    PrefetchQueryMemory(componentBytes + prefetchOffset * componentSize);
}

void PrefetchMutableBatch(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::span<const std::size_t> componentSizes,
    const MutableQueryComponentPointerBlock& fieldComponents,
    std::size_t offset,
    std::size_t prefetchDistance) noexcept {
    if (prefetchDistance == 0) {
        return;
    }

    if (prefetchDistance >= entityCount - offset) {
        return;
    }

    const std::size_t prefetchOffset = offset + prefetchDistance;
    PrefetchQueryMemory(entityIds + prefetchOffset);
    for (std::size_t field = 0; field < componentSizes.size(); ++field) {
        auto* bytes = static_cast<std::uint8_t*>(fieldComponents[field]);
        PrefetchQueryMemory(bytes + prefetchOffset * componentSizes[field], QueryPrefetchAccess::Write);
    }
}

void PrefetchMutableSingleComponent(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::uint8_t* componentBytes,
    std::size_t componentSize,
    std::size_t offset,
    std::size_t prefetchDistance) noexcept {
    if (prefetchDistance == 0 || prefetchDistance >= entityCount - offset) {
        return;
    }

    const std::size_t prefetchOffset = offset + prefetchDistance;
    PrefetchQueryMemory(entityIds + prefetchOffset);
    PrefetchQueryMemory(componentBytes + prefetchOffset * componentSize, QueryPrefetchAccess::Write);
}

void DispatchReadOnlySingleComponent(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::size_t componentSize,
    const void* component,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    QueryComponentPointerBlock batchComponents{};
    const auto* componentBytes = static_cast<const std::uint8_t*>(component);

    for (std::size_t offset = 0; offset < entityCount; offset += maxBatchSize) {
        const std::size_t batchCount = std::min(maxBatchSize, entityCount - offset);
        PrefetchReadOnlySingleComponent(entityIds, entityCount, componentBytes, componentSize, offset, prefetchDistance);
        batchComponents[0] = componentBytes + offset * componentSize;
        visitor(entityIds + offset, batchCount, batchComponents.data(), context);
    }
}

void DispatchMutableSingleComponent(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::size_t componentSize,
    void* component,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    MutableQueryComponentPointerBlock batchComponents{};
    auto* componentBytes = static_cast<std::uint8_t*>(component);

    for (std::size_t offset = 0; offset < entityCount; offset += maxBatchSize) {
        const std::size_t batchCount = std::min(maxBatchSize, entityCount - offset);
        PrefetchMutableSingleComponent(entityIds, entityCount, componentBytes, componentSize, offset, prefetchDistance);
        batchComponents[0] = componentBytes + offset * componentSize;
        visitor(entityIds + offset, batchCount, batchComponents.data(), context);
    }
}

} // namespace

void QueryTableBatchDispatcher::Dispatch(
    const ecs_iter_t& iterator,
    std::span<const std::size_t> componentSizes,
    const QueryComponentPointerBlock& fieldComponents,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    Dispatch(
        reinterpret_cast<const Entity::IdType*>(iterator.entities),
        static_cast<std::size_t>(iterator.count),
        componentSizes,
        fieldComponents,
        maxBatchSize,
        prefetchDistance,
        visitor,
        context);
}

void QueryTableBatchDispatcher::Dispatch(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::span<const std::size_t> componentSizes,
    const QueryComponentPointerBlock& fieldComponents,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawBatchVisitor visitor,
    void* context) {
    if (entityIds == nullptr || entityCount == 0 || componentSizes.empty() || maxBatchSize == 0 || visitor == nullptr) {
        return;
    }

    if (componentSizes.size() == 1) {
        DispatchReadOnlySingleComponent(
            entityIds,
            entityCount,
            componentSizes.front(),
            fieldComponents[0],
            maxBatchSize,
            prefetchDistance,
            visitor,
            context);
        return;
    }

    QueryComponentPointerBlock batchComponents{};

    for (std::size_t offset = 0; offset < entityCount; offset += maxBatchSize) {
        const std::size_t batchCount = std::min(maxBatchSize, entityCount - offset);
        PrefetchReadOnlyBatch(entityIds, entityCount, componentSizes, fieldComponents, offset, prefetchDistance);
        for (std::size_t field = 0; field < componentSizes.size(); ++field) {
            const auto* bytes = static_cast<const std::uint8_t*>(fieldComponents[field]);
            batchComponents[field] = bytes + offset * componentSizes[field];
        }
        visitor(entityIds + offset, batchCount, batchComponents.data(), context);
    }
}

void QueryTableBatchDispatcher::DispatchMutable(
    const ecs_iter_t& iterator,
    std::span<const std::size_t> componentSizes,
    const MutableQueryComponentPointerBlock& fieldComponents,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    DispatchMutable(
        reinterpret_cast<const Entity::IdType*>(iterator.entities),
        static_cast<std::size_t>(iterator.count),
        componentSizes,
        fieldComponents,
        maxBatchSize,
        prefetchDistance,
        visitor,
        context);
}

void QueryTableBatchDispatcher::DispatchMutable(
    const Entity::IdType* entityIds,
    std::size_t entityCount,
    std::span<const std::size_t> componentSizes,
    const MutableQueryComponentPointerBlock& fieldComponents,
    std::size_t maxBatchSize,
    std::size_t prefetchDistance,
    QueryRawMutableBatchVisitor visitor,
    void* context) {
    if (entityIds == nullptr || entityCount == 0 || componentSizes.empty() || maxBatchSize == 0 || visitor == nullptr) {
        return;
    }

    if (componentSizes.size() == 1) {
        DispatchMutableSingleComponent(
            entityIds,
            entityCount,
            componentSizes.front(),
            fieldComponents[0],
            maxBatchSize,
            prefetchDistance,
            visitor,
            context);
        return;
    }

    MutableQueryComponentPointerBlock batchComponents{};

    for (std::size_t offset = 0; offset < entityCount; offset += maxBatchSize) {
        const std::size_t batchCount = std::min(maxBatchSize, entityCount - offset);
        PrefetchMutableBatch(entityIds, entityCount, componentSizes, fieldComponents, offset, prefetchDistance);
        for (std::size_t field = 0; field < componentSizes.size(); ++field) {
            auto* bytes = static_cast<std::uint8_t*>(fieldComponents[field]);
            batchComponents[field] = bytes + offset * componentSizes[field];
        }
        visitor(entityIds + offset, batchCount, batchComponents.data(), context);
    }
}

} // namespace kb::ecs

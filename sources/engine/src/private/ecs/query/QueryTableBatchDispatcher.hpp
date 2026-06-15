#pragma once

#include "ecs/query/QueryLimits.hpp"
#include "engine/ecs/Query.hpp"

#include <cstddef>
#include <span>

struct ecs_iter_t;

namespace kb::ecs {

class QueryTableBatchDispatcher {
public:
    static void Dispatch(
        const ecs_iter_t& iterator,
        std::span<const std::size_t> componentSizes,
        const QueryComponentPointerBlock& fieldComponents,
        std::size_t maxBatchSize,
        std::size_t prefetchDistance,
        QueryRawBatchVisitor visitor,
        void* context);

    static void Dispatch(
        const Entity::IdType* entityIds,
        std::size_t entityCount,
        std::span<const std::size_t> componentSizes,
        const QueryComponentPointerBlock& fieldComponents,
        std::size_t maxBatchSize,
        std::size_t prefetchDistance,
        QueryRawBatchVisitor visitor,
        void* context);

    static void DispatchMutable(
        const ecs_iter_t& iterator,
        std::span<const std::size_t> componentSizes,
        const MutableQueryComponentPointerBlock& fieldComponents,
        std::size_t maxBatchSize,
        std::size_t prefetchDistance,
        QueryRawMutableBatchVisitor visitor,
        void* context);

    static void DispatchMutable(
        const Entity::IdType* entityIds,
        std::size_t entityCount,
        std::span<const std::size_t> componentSizes,
        const MutableQueryComponentPointerBlock& fieldComponents,
        std::size_t maxBatchSize,
        std::size_t prefetchDistance,
        QueryRawMutableBatchVisitor visitor,
        void* context);
};

} // namespace kb::ecs

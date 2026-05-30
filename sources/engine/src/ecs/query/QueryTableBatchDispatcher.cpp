#include "ecs/query/QueryTableBatchDispatcher.hpp"

#include <flecs.h>

#include <algorithm>
#include <cstdint>

namespace kb::ecs {

void QueryTableBatchDispatcher::Dispatch(
    const ecs_iter_t& iterator,
    std::span<const std::size_t> componentSizes,
    const QueryComponentPointerBlock& fieldComponents,
    std::size_t maxBatchSize,
    QueryRawBatchVisitor visitor,
    void* context) {
    QueryComponentPointerBlock batchComponents{};
    const std::size_t tableCount = static_cast<std::size_t>(iterator.count);

    for (std::size_t offset = 0; offset < tableCount; offset += maxBatchSize) {
        const std::size_t batchCount = std::min(maxBatchSize, tableCount - offset);
        for (std::size_t field = 0; field < componentSizes.size(); ++field) {
            const auto* bytes = static_cast<const std::uint8_t*>(fieldComponents[field]);
            batchComponents[field] = bytes + offset * componentSizes[field];
        }
        visitor(reinterpret_cast<const Entity::IdType*>(iterator.entities) + offset, batchCount, batchComponents.data(), context);
    }
}

} // namespace kb::ecs

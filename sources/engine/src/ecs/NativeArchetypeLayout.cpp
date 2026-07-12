#include "ecs/NativeArchetypeLayout.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace kb::ecs::detail {
namespace {

constexpr std::size_t kChunkAlignment = 64;

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] std::size_t PayloadBytesForCapacity(std::span<const NativeComponentType> types, std::size_t capacity) {
    std::size_t offset = 0;
    for (const NativeComponentType& type : types) {
        offset = AlignUp(offset, type.alignment);
        offset += type.size * capacity;
    }
    return offset;
}

[[nodiscard]] std::size_t HotPayloadBytesForCapacity(std::span<const NativeComponentType> types, std::size_t capacity) {
    std::size_t offset = 0;
    bool hasHotTableComponent = false;
    for (const NativeComponentType& type : types) {
        if (!UsesHotChunkPayload(type.storageClass)) {
            continue;
        }
        hasHotTableComponent = true;
        offset = AlignUp(offset, type.alignment);
        offset += type.size * capacity;
    }

    return hasHotTableComponent ? offset : capacity * sizeof(Entity);
}

[[nodiscard]] std::size_t FindPayloadCapacity(
    std::span<const NativeComponentType> types,
    std::size_t payloadBytes,
    std::size_t bytesPerEntity,
    std::size_t (*payloadForCapacity)(std::span<const NativeComponentType>, std::size_t)) {
    std::size_t low = 0;
    std::size_t high = std::max<std::size_t>(1, payloadBytes / std::max<std::size_t>(bytesPerEntity, 1));
    while (payloadForCapacity(types, high) <= payloadBytes && high < (std::numeric_limits<std::size_t>::max() / 2U)) {
        high *= 2U;
    }

    while (low < high) {
        const std::size_t mid = low + ((high - low + 1U) / 2U);
        if (payloadForCapacity(types, mid) <= payloadBytes) {
            low = mid;
        } else {
            high = mid - 1U;
        }
    }

    return low;
}

} // namespace

void ValidateNativeComponentType(const NativeComponentType& type) {
    if (type.id == 0 || type.size == 0 || type.alignment == 0 || !std::has_single_bit(type.alignment)) {
        throw std::invalid_argument("Invalid native ECS component type");
    }
    if (type.alignment > kChunkAlignment) {
        throw std::invalid_argument("Native ECS component alignment exceeds chunk alignment");
    }
    if ((type.size % type.alignment) != 0U) {
        throw std::invalid_argument("Native ECS component size must preserve row alignment");
    }
}

ArchetypeLayout BuildNativeArchetypeLayout(std::span<const NativeComponentType> types, std::size_t payloadBytes) {
    ArchetypeLayout layout;
    for (const NativeComponentType& type : types) {
        layout.bytesPerEntity += type.size;
        if (UsesHotChunkPayload(type.storageClass)) {
            layout.hotBytesPerEntity += type.size;
        } else {
            layout.nonHotBytesPerEntity += type.size;
        }
    }
    if (types.empty()) {
        layout.capacity = std::max<std::size_t>(payloadBytes / sizeof(Entity), 1U);
        layout.hotOnlyCapacity = layout.capacity;
        return layout;
    }

    const std::size_t hotCapacityBaseline = layout.hotBytesPerEntity == 0U ? sizeof(Entity) : layout.hotBytesPerEntity;
    const std::size_t fullCapacity = FindPayloadCapacity(types, payloadBytes, layout.bytesPerEntity, PayloadBytesForCapacity);
    layout.hotOnlyCapacity = FindPayloadCapacity(types, payloadBytes, hotCapacityBaseline, HotPayloadBytesForCapacity);
    if (layout.hotOnlyCapacity == 0U) {
        throw std::invalid_argument("Native ECS chunk payload is too small for archetype hot storage");
    }
    layout.capacity = layout.hotOnlyCapacity;
    layout.capacityLostToNonHotStorage = layout.hotOnlyCapacity > fullCapacity ? layout.hotOnlyCapacity - fullCapacity : 0U;
    std::size_t hotPayloadOffset = 0;
    std::size_t sidePayloadOffset = 0;
    std::size_t hotOffset = 0;
    std::size_t nonHotPayloadBytes = 0;
    for (const NativeComponentType& type : types) {
        if (UsesHotChunkPayload(type.storageClass)) {
            hotPayloadOffset = AlignUp(hotPayloadOffset, type.alignment);
            layout.columns.push_back(ComponentLayout{ .type = type, .offset = hotPayloadOffset, .primaryPayload = true });
            const std::size_t columnPayloadBytes = type.size * layout.capacity;
            hotPayloadOffset += columnPayloadBytes;
            hotOffset = AlignUp(hotOffset, type.alignment);
            hotOffset += type.size * layout.hotOnlyCapacity;
        } else {
            sidePayloadOffset = AlignUp(sidePayloadOffset, type.alignment);
            layout.columns.push_back(ComponentLayout{ .type = type, .offset = sidePayloadOffset, .primaryPayload = false });
            const std::size_t columnPayloadBytes = type.size * layout.capacity;
            sidePayloadOffset += columnPayloadBytes;
            nonHotPayloadBytes += columnPayloadBytes;
        }
    }
    layout.usedPayloadBytes = hotPayloadOffset;
    layout.hotOnlyUsedPayloadBytes = layout.hotBytesPerEntity == 0U ? layout.hotOnlyCapacity * sizeof(Entity) : hotOffset;
    layout.nonHotUsedPayloadBytes = nonHotPayloadBytes;
    layout.sidePayloadBytes = sidePayloadOffset;
    return layout;
}

} // namespace kb::ecs::detail

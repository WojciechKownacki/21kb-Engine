#pragma once

#include "engine/ecs/NativeArchetypeStorage.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace kb::ecs::detail {

struct ComponentLayout {
    NativeComponentType type{};
    std::size_t offset = 0;
    bool primaryPayload = true;
};

struct ArchetypeLayout {
    std::vector<ComponentLayout> columns;
    std::size_t capacity = 0;
    std::size_t hotOnlyCapacity = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t bytesPerEntity = 0;
    std::size_t hotBytesPerEntity = 0;
    std::size_t nonHotBytesPerEntity = 0;
    std::size_t usedPayloadBytes = 0;
    std::size_t hotOnlyUsedPayloadBytes = 0;
    std::size_t nonHotUsedPayloadBytes = 0;
    std::size_t sidePayloadBytes = 0;
};

void ValidateNativeComponentType(const NativeComponentType& type);
[[nodiscard]] ArchetypeLayout BuildNativeArchetypeLayout(
    std::span<const NativeComponentType> types,
    std::size_t payloadBytes);

} // namespace kb::ecs::detail

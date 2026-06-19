#pragma once

#include "engine/ecs/ChunkSizeProfile.hpp"
#include "engine/ecs/NativeArchetypeStorage.hpp"
#include "engine/ecs/WorldConfig.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <span>

namespace kb::ecs {

enum class ChunkSizeTuningWorkload : std::uint8_t {
    SequentialHotLoop,
    StructuralChurn,
    Streaming,
    MobileThermal,
};

struct ChunkSizeTuningInput {
    std::span<const NativeComponentType> components;
    std::size_t entityCount = 0;
    ChunkSizeTuningWorkload workload = ChunkSizeTuningWorkload::SequentialHotLoop;
    std::size_t maxChunkPayloadBytes = 0;
    double targetOccupancyPercent = 92.0;
};

struct ChunkSizeTuningCandidate {
    ChunkSizeProfile profile = kDefaultChunkSizeProfile;
    std::size_t payloadBytes = 0;
    std::size_t bytesPerEntity = 0;
    std::size_t hotBytesPerEntity = 0;
    std::size_t nonHotBytesPerEntity = 0;
    std::size_t capacity = 0;
    std::size_t hotOnlyCapacity = 0;
    std::size_t capacityLostToNonHotStorage = 0;
    std::size_t estimatedChunks = 0;
    std::size_t estimatedAllocatedBytes = 0;
    std::size_t estimatedUsedBytes = 0;
    std::size_t estimatedWastedBytes = 0;
    std::size_t estimatedSparseChunks = 0;
    double occupancyPercent = 0.0;
    double score = std::numeric_limits<double>::infinity();
    bool valid = false;
};

struct ChunkSizeTuningResult {
    ChunkSizeTuningCandidate recommendation{};
    std::array<ChunkSizeTuningCandidate, static_cast<std::size_t>(ChunkSizeProfile::Count)> candidates{};
    std::size_t candidateCount = 0;
};

struct ChunkSizeAutoTunedWorldConfig {
    WorldConfig config{};
    ChunkSizeTuningResult tuning{};
    bool applied = false;
};

[[nodiscard]] constexpr std::array<ChunkSizeProfile, static_cast<std::size_t>(ChunkSizeProfile::Count)> AllChunkSizeProfiles() noexcept {
    return {
        ChunkSizeProfile::Chunk4KB,
        ChunkSizeProfile::Chunk8KB,
        ChunkSizeProfile::Chunk16KB,
        ChunkSizeProfile::Chunk32KB,
        ChunkSizeProfile::Chunk64KB,
        ChunkSizeProfile::Chunk128KB,
        ChunkSizeProfile::Chunk256KB,
        ChunkSizeProfile::Chunk512KB,
    };
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningAlignUp(std::size_t value, std::size_t alignment) noexcept {
    return alignment == 0U ? value : ((value + alignment - 1U) / alignment) * alignment;
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningBytesPerEntity(std::span<const NativeComponentType> components) noexcept {
    std::size_t bytesPerEntity = 0;
    for (const NativeComponentType& component : components) {
        bytesPerEntity += component.size;
    }
    return components.empty() ? sizeof(Entity) : bytesPerEntity;
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningHotBytesPerEntity(std::span<const NativeComponentType> components) noexcept {
    std::size_t bytesPerEntity = 0;
    for (const NativeComponentType& component : components) {
        if (UsesHotChunkPayload(component.storageClass)) {
            bytesPerEntity += component.size;
        }
    }
    return bytesPerEntity;
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningNonHotBytesPerEntity(std::span<const NativeComponentType> components) noexcept {
    std::size_t bytesPerEntity = 0;
    for (const NativeComponentType& component : components) {
        if (UsesNativeSideStorage(component.storageClass)) {
            bytesPerEntity += component.size;
        }
    }
    return bytesPerEntity;
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningPayloadForCapacity(
    std::span<const NativeComponentType> components,
    std::size_t capacity) noexcept {
    if (components.empty()) {
        return capacity * sizeof(Entity);
    }

    std::size_t offset = 0;
    for (const NativeComponentType& component : components) {
        offset = ChunkSizeTuningAlignUp(offset, component.alignment);
        offset += component.size * capacity;
    }
    return offset;
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningHotPayloadForCapacity(
    std::span<const NativeComponentType> components,
    std::size_t capacity) noexcept {
    std::size_t offset = 0;
    bool hasHotComponent = false;
    for (const NativeComponentType& component : components) {
        if (!UsesHotChunkPayload(component.storageClass)) {
            continue;
        }
        hasHotComponent = true;
        offset = ChunkSizeTuningAlignUp(offset, component.alignment);
        offset += component.size * capacity;
    }
    return hasHotComponent ? offset : capacity * sizeof(Entity);
}

[[nodiscard]] constexpr std::size_t ChunkSizeTuningNonHotPayloadForCapacity(
    std::span<const NativeComponentType> components,
    std::size_t capacity) noexcept {
    std::size_t offset = 0;
    for (const NativeComponentType& component : components) {
        if (UsesHotChunkPayload(component.storageClass)) {
            continue;
        }
        offset = ChunkSizeTuningAlignUp(offset, component.alignment);
        offset += component.size * capacity;
    }
    return offset;
}

[[nodiscard]] constexpr std::size_t EstimateChunkCapacity(
    ChunkSizeProfile profile,
    std::span<const NativeComponentType> components) noexcept {
    const std::size_t payloadBytes = ChunkPayloadBytes(profile);
    if (payloadBytes == 0U) {
        return 0;
    }
    if (components.empty()) {
        return payloadBytes / sizeof(Entity);
    }

    std::size_t bytesPerEntity = 0;
    for (const NativeComponentType& component : components) {
        if (component.size == 0U || component.alignment == 0U) {
            return 0;
        }
        bytesPerEntity += component.size;
    }

    std::size_t low = 0;
    std::size_t high = payloadBytes / (bytesPerEntity == 0U ? 1U : bytesPerEntity);
    while (low < high) {
        const std::size_t mid = low + ((high - low + 1U) / 2U);
        if (ChunkSizeTuningPayloadForCapacity(components, mid) <= payloadBytes) {
            low = mid;
        } else {
            high = mid - 1U;
        }
    }
    return low;
}

[[nodiscard]] constexpr std::size_t EstimateHotOnlyChunkCapacity(
    ChunkSizeProfile profile,
    std::span<const NativeComponentType> components) noexcept {
    const std::size_t payloadBytes = ChunkPayloadBytes(profile);
    if (payloadBytes == 0U) {
        return 0;
    }
    if (components.empty()) {
        return payloadBytes / sizeof(Entity);
    }

    const std::size_t hotBytesPerEntity = ChunkSizeTuningHotBytesPerEntity(components);
    const std::size_t baselineBytesPerEntity = hotBytesPerEntity == 0U ? sizeof(Entity) : hotBytesPerEntity;
    std::size_t low = 0;
    std::size_t high = payloadBytes / baselineBytesPerEntity;
    while (low < high) {
        const std::size_t mid = low + ((high - low + 1U) / 2U);
        if (ChunkSizeTuningHotPayloadForCapacity(components, mid) <= payloadBytes) {
            low = mid;
        } else {
            high = mid - 1U;
        }
    }
    return low;
}

[[nodiscard]] constexpr double ChunkSizeTuningCachePenalty(ChunkSizeTuningWorkload workload, std::size_t payloadBytes) noexcept {
    switch (workload) {
    case ChunkSizeTuningWorkload::SequentialHotLoop:
        return payloadBytes <= (64U * 1024U) ? 0.0 : 0.015;
    case ChunkSizeTuningWorkload::StructuralChurn:
        return payloadBytes <= (32U * 1024U) ? 0.0 : 0.080;
    case ChunkSizeTuningWorkload::Streaming:
        return payloadBytes <= (128U * 1024U) ? 0.0 : 0.010;
    case ChunkSizeTuningWorkload::MobileThermal:
        return payloadBytes <= (16U * 1024U) ? 0.0 : 0.180;
    }
    return 0.0;
}

[[nodiscard]] constexpr double ChunkSizeTuningTraversalPenalty(ChunkSizeTuningWorkload workload) noexcept {
    switch (workload) {
    case ChunkSizeTuningWorkload::SequentialHotLoop:
        return 384.0;
    case ChunkSizeTuningWorkload::StructuralChurn:
        return 96.0;
    case ChunkSizeTuningWorkload::Streaming:
        return 768.0;
    case ChunkSizeTuningWorkload::MobileThermal:
        return 128.0;
    }
    return 256.0;
}

[[nodiscard]] constexpr ChunkSizeTuningCandidate EvaluateChunkSizeProfile(
    const ChunkSizeTuningInput& input,
    ChunkSizeProfile profile) noexcept {
    ChunkSizeTuningCandidate candidate;
    candidate.profile = profile;
    candidate.payloadBytes = ChunkPayloadBytes(profile);
    candidate.bytesPerEntity = ChunkSizeTuningBytesPerEntity(input.components);
    candidate.hotBytesPerEntity = ChunkSizeTuningHotBytesPerEntity(input.components);
    candidate.nonHotBytesPerEntity = ChunkSizeTuningNonHotBytesPerEntity(input.components);
    const std::size_t coLocatedCapacity = EstimateChunkCapacity(profile, input.components);
    candidate.capacity = EstimateHotOnlyChunkCapacity(profile, input.components);
    candidate.hotOnlyCapacity = EstimateHotOnlyChunkCapacity(profile, input.components);
    candidate.capacityLostToNonHotStorage = candidate.hotOnlyCapacity > coLocatedCapacity ? candidate.hotOnlyCapacity - coLocatedCapacity : 0U;
    if (candidate.payloadBytes == 0U || candidate.capacity == 0U ||
        (input.maxChunkPayloadBytes != 0U && candidate.payloadBytes > input.maxChunkPayloadBytes)) {
        return candidate;
    }

    candidate.valid = true;
    candidate.estimatedChunks = input.entityCount == 0U ? 0U : ((input.entityCount + candidate.capacity - 1U) / candidate.capacity);
    candidate.estimatedAllocatedBytes = candidate.estimatedChunks *
        (candidate.payloadBytes + ChunkSizeTuningNonHotPayloadForCapacity(input.components, candidate.capacity));
    candidate.estimatedUsedBytes = input.entityCount * candidate.bytesPerEntity;
    candidate.estimatedWastedBytes = candidate.estimatedAllocatedBytes > candidate.estimatedUsedBytes
        ? candidate.estimatedAllocatedBytes - candidate.estimatedUsedBytes
        : 0U;
    candidate.estimatedSparseChunks = (input.entityCount == 0U || (input.entityCount % candidate.capacity) == 0U) ? 0U : 1U;
    candidate.occupancyPercent = candidate.estimatedAllocatedBytes == 0U
        ? 100.0
        : (static_cast<double>(candidate.estimatedUsedBytes) * 100.0) / static_cast<double>(candidate.estimatedAllocatedBytes);

    const double occupancyMiss = candidate.occupancyPercent >= input.targetOccupancyPercent
        ? 0.0
        : (input.targetOccupancyPercent - candidate.occupancyPercent) * 1024.0;
    candidate.score = static_cast<double>(candidate.estimatedWastedBytes) +
        (static_cast<double>(candidate.estimatedChunks) * ChunkSizeTuningTraversalPenalty(input.workload)) +
        (static_cast<double>(candidate.payloadBytes) * ChunkSizeTuningCachePenalty(input.workload, candidate.payloadBytes)) +
        occupancyMiss;
    return candidate;
}

[[nodiscard]] constexpr ChunkSizeTuningResult TuneChunkSizeProfile(const ChunkSizeTuningInput& input) noexcept {
    ChunkSizeTuningResult result;
    result.recommendation.score = std::numeric_limits<double>::infinity();
    for (ChunkSizeProfile profile : AllChunkSizeProfiles()) {
        ChunkSizeTuningCandidate candidate = EvaluateChunkSizeProfile(input, profile);
        result.candidates[result.candidateCount++] = candidate;
        if (candidate.valid && candidate.score < result.recommendation.score) {
            result.recommendation = candidate;
        }
    }
    return result;
}

[[nodiscard]] constexpr ChunkSizeAutoTunedWorldConfig AutoTuneWorldConfig(
    WorldConfig baseConfig,
    const ChunkSizeTuningInput& input) noexcept {
    ChunkSizeAutoTunedWorldConfig tuned;
    tuned.config = baseConfig;
    tuned.tuning = TuneChunkSizeProfile(input);
    tuned.applied = tuned.tuning.recommendation.valid;
    if (tuned.applied) {
        tuned.config.chunkSizeProfile = tuned.tuning.recommendation.profile;
    }
    return tuned;
}

} // namespace kb::ecs

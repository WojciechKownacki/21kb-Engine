#pragma once

#include "engine/ecs/QueryExecutionSettings.hpp"

#include <algorithm>
#include <cstddef>

namespace kb::ecs {

enum class QueryExecutionWorkloadClass {
    ReadOnlyMemory,
    LinearWrite,
    MediumKernel,
    DenseMatrixWrite,
    HeavyTransform,
    StructuralChange,
};

struct QueryExecutionTuningInput {
    QueryExecutionWorkloadClass workload = QueryExecutionWorkloadClass::MediumKernel;
    std::size_t entityCount = 0U;
    std::size_t workerCount = 1U;
    std::size_t fallbackGrainSize = kDefaultQueryExecutionGrainSize;
    std::size_t minChunksPerWorker = 4U;
};

struct QueryExecutionGrainLimits {
    std::size_t minimum = 1U;
    std::size_t preferred = kDefaultQueryExecutionGrainSize;
    std::size_t maximum = kDefaultQueryExecutionGrainSize;
};

inline constexpr std::size_t kLinearWriteSingleThreadEntityLimit = 2U * 1024U * 1024U;
inline constexpr std::size_t kDenseMatrixWriteSingleThreadEntityLimit = 64U * 1024U;

[[nodiscard]] constexpr QueryExecutionGrainLimits QueryExecutionWorkloadGrainLimits(QueryExecutionWorkloadClass workload) noexcept {
    switch (workload) {
    case QueryExecutionWorkloadClass::ReadOnlyMemory:
        return QueryExecutionGrainLimits{ .minimum = 8U * 1024U, .preferred = 32U * 1024U, .maximum = 64U * 1024U };
    case QueryExecutionWorkloadClass::LinearWrite:
        return QueryExecutionGrainLimits{ .minimum = 4U * 1024U, .preferred = 16U * 1024U, .maximum = 32U * 1024U };
    case QueryExecutionWorkloadClass::MediumKernel:
        return QueryExecutionGrainLimits{ .minimum = 2U * 1024U, .preferred = 8U * 1024U, .maximum = 16U * 1024U };
    case QueryExecutionWorkloadClass::DenseMatrixWrite:
        return QueryExecutionGrainLimits{ .minimum = 4U * 1024U, .preferred = 16U * 1024U, .maximum = 64U * 1024U };
    case QueryExecutionWorkloadClass::HeavyTransform:
        return QueryExecutionGrainLimits{ .minimum = 512U, .preferred = 2U * 1024U, .maximum = 4U * 1024U };
    case QueryExecutionWorkloadClass::StructuralChange:
        return QueryExecutionGrainLimits{ .minimum = 512U, .preferred = 2U * 1024U, .maximum = 8U * 1024U };
    }
    return QueryExecutionGrainLimits{ .minimum = 1U, .preferred = kDefaultQueryExecutionGrainSize, .maximum = kDefaultQueryExecutionGrainSize };
}

[[nodiscard]] constexpr std::size_t CeilDivide(std::size_t value, std::size_t divisor) noexcept {
    return divisor == 0U ? value : (value + divisor - 1U) / divisor;
}

[[nodiscard]] constexpr std::size_t ResolveQueryExecutionGrainSize(QueryExecutionTuningInput input) noexcept {
    if (input.entityCount == 0U) {
        return input.fallbackGrainSize == 0U ? kDefaultQueryExecutionGrainSize : input.fallbackGrainSize;
    }

    const QueryExecutionGrainLimits limits = QueryExecutionWorkloadGrainLimits(input.workload);
    const std::size_t workerCount = std::max<std::size_t>(input.workerCount, 1U);
    const std::size_t chunksPerWorker = std::max<std::size_t>(input.minChunksPerWorker, 1U);
    const std::size_t balancedByWorkers = CeilDivide(input.entityCount, workerCount * chunksPerWorker);
    const std::size_t fallback = input.fallbackGrainSize == 0U ? limits.preferred : input.fallbackGrainSize;
    const std::size_t preferred = std::max(limits.minimum, std::min(limits.maximum, fallback));
    const std::size_t candidate = std::max(limits.minimum, std::min(limits.maximum, std::max(preferred, balancedByWorkers)));
    return std::min(input.entityCount, candidate);
}

[[nodiscard]] constexpr QueryExecutionPolicy ResolveQueryExecutionPolicy(
    QueryExecutionSettings settings,
    QueryExecutionTuningInput input) noexcept {
    // Small parallel workloads pay more in scheduling overhead than they gain;
    // collapse them to serial execution. The kernel backend (not the policy)
    // drives SIMD in the hot path, so SingleThread keeps the measured fast path
    // intact while remaining vectorized.
    if (input.workload == QueryExecutionWorkloadClass::LinearWrite &&
        input.entityCount != 0U &&
        input.entityCount <= kLinearWriteSingleThreadEntityLimit &&
        QueryExecutionPolicyUsesParallelism(settings.policy)) {
        return QueryExecutionPolicy::SingleThread;
    }
    if (input.workload == QueryExecutionWorkloadClass::DenseMatrixWrite &&
        input.entityCount != 0U &&
        input.entityCount <= kDenseMatrixWriteSingleThreadEntityLimit &&
        QueryExecutionPolicyUsesParallelism(settings.policy)) {
        return QueryExecutionPolicy::SingleThread;
    }
    return settings.policy;
}

[[nodiscard]] constexpr QueryExecutionSettings TuneQueryExecutionSettings(
    QueryExecutionSettings settings,
    QueryExecutionTuningInput input) noexcept {
    input.fallbackGrainSize = settings.maxBatchSize == 0U ? input.fallbackGrainSize : settings.maxBatchSize;
    settings.maxBatchSize = ResolveQueryExecutionGrainSize(input);
    settings.policy = ResolveQueryExecutionPolicy(settings, input);
    if (settings.policy == QueryExecutionPolicy::SingleThread &&
        input.entityCount != 0U &&
        input.workload == QueryExecutionWorkloadClass::LinearWrite) {
        settings.maxBatchSize = input.entityCount;
    }
    return settings;
}

} // namespace kb::ecs

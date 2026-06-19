#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace kb::ecs {

inline constexpr double kEcsBandwidthGigabyte = 1000.0 * 1000.0 * 1000.0;

struct MemoryTrafficEstimate {
    std::uint64_t entityCount = 0;
    std::uint64_t bytesRead = 0;
    std::uint64_t bytesWritten = 0;

    [[nodiscard]] constexpr std::uint64_t TotalBytes() const noexcept {
        return bytesRead + bytesWritten;
    }

    [[nodiscard]] constexpr double BytesPerEntity() const noexcept {
        return entityCount == 0U ? 0.0 : static_cast<double>(TotalBytes()) / static_cast<double>(entityCount);
    }
};

struct MemoryBandwidthSample {
    std::uint64_t bytesTouched = 0;
    std::uint64_t elapsedNanoseconds = 0;

    [[nodiscard]] constexpr double BytesPerSecond() const noexcept {
        return elapsedNanoseconds == 0U
            ? 0.0
            : (static_cast<double>(bytesTouched) * 1000000000.0) / static_cast<double>(elapsedNanoseconds);
    }

    [[nodiscard]] constexpr double GigabytesPerSecond() const noexcept {
        return BytesPerSecond() / kEcsBandwidthGigabyte;
    }
};

struct RooflineReport {
    MemoryBandwidthSample achieved;
    MemoryBandwidthSample baseline;
    double targetBaselinePercent = 0.0;

    [[nodiscard]] constexpr double BaselinePercent() const noexcept {
        const double baselineBytesPerSecond = baseline.BytesPerSecond();
        return baselineBytesPerSecond == 0.0 ? 0.0 : (achieved.BytesPerSecond() / baselineBytesPerSecond) * 100.0;
    }

    [[nodiscard]] constexpr bool MeetsTarget() const noexcept {
        return BaselinePercent() >= targetBaselinePercent;
    }
};

[[nodiscard]] constexpr MemoryTrafficEstimate EstimateMemoryTraffic(
    std::uint64_t entityCount,
    std::uint64_t bytesReadPerEntity,
    std::uint64_t bytesWrittenPerEntity) noexcept {
    return MemoryTrafficEstimate{
        .entityCount = entityCount,
        .bytesRead = entityCount * bytesReadPerEntity,
        .bytesWritten = entityCount * bytesWrittenPerEntity,
    };
}

[[nodiscard]] inline MemoryTrafficEstimate EstimateReadOnlyQueryTraffic(
    std::uint64_t entityCount,
    std::span<const std::size_t> componentSizes) noexcept {
    std::uint64_t bytesReadPerEntity = 0U;
    for (const std::size_t componentSize : componentSizes) {
        bytesReadPerEntity += static_cast<std::uint64_t>(componentSize);
    }
    return EstimateMemoryTraffic(entityCount, bytesReadPerEntity, 0U);
}

[[nodiscard]] inline MemoryTrafficEstimate EstimateMutableQueryTraffic(
    std::uint64_t entityCount,
    std::span<const std::size_t> componentSizes) noexcept {
    std::uint64_t bytesPerEntity = 0U;
    for (const std::size_t componentSize : componentSizes) {
        bytesPerEntity += static_cast<std::uint64_t>(componentSize);
    }
    return EstimateMemoryTraffic(entityCount, bytesPerEntity, bytesPerEntity);
}

[[nodiscard]] constexpr MemoryBandwidthSample EstimateMemoryBandwidth(
    std::uint64_t bytesTouched,
    std::uint64_t elapsedNanoseconds) noexcept {
    return MemoryBandwidthSample{
        .bytesTouched = bytesTouched,
        .elapsedNanoseconds = elapsedNanoseconds,
    };
}

[[nodiscard]] constexpr RooflineReport BuildRooflineReport(
    MemoryBandwidthSample achieved,
    MemoryBandwidthSample baseline,
    double targetBaselinePercent) noexcept {
    return RooflineReport{
        .achieved = achieved,
        .baseline = baseline,
        .targetBaselinePercent = targetBaselinePercent,
    };
}

} // namespace kb::ecs

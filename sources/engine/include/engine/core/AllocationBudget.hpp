#pragma once

#include <cstddef>

namespace kb::core {
struct AllocationTelemetry {
    // A zero budget deliberately means "no allocations permitted", rather
    // than an undocumented unlimited fallback. Callers that need an
    // unbounded development probe must choose std::numeric_limits<size_t>::max().
    std::size_t budget = 0U;
    std::size_t used = 0U;
    std::size_t peak = 0U;
    std::size_t allocationCount = 0U;
    std::size_t rejectedAllocationCount = 0U;
    std::size_t rejectedBytes = 0U;

    [[nodiscard]] bool Reserve(std::size_t bytes) noexcept {
        if (bytes > budget - used) {
            ++rejectedAllocationCount;
            rejectedBytes += bytes;
            return false;
        }
        used += bytes;
        peak = used > peak ? used : peak;
        ++allocationCount;
        return true;
    }

    void Release(std::size_t bytes) noexcept {
        used = bytes > used ? 0U : used - bytes;
    }
};
} // namespace kb::core

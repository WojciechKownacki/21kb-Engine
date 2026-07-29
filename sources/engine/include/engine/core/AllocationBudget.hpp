#pragma once

#include <cstddef>

namespace kb::core {
struct AllocationTelemetry { std::size_t budget=0U; std::size_t used=0U; [[nodiscard]] bool Reserve(std::size_t bytes) noexcept { if(bytes>budget-used)return false;used+=bytes;return true;} void Release(std::size_t bytes) noexcept { used=bytes>used?0U:used-bytes;} };
} // namespace kb::core

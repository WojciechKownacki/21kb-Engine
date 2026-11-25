#pragma once

#include <cstddef>
#include <cstdint>

namespace kb::network {
struct NetworkSecurityLimits { std::size_t maxPayloadBytes = 1200U; std::uint32_t maxMessagesPerTick = 64U; };
[[nodiscard]] constexpr bool IsValidSecurityLimits(NetworkSecurityLimits limits) noexcept { return limits.maxPayloadBytes>0U&&limits.maxPayloadBytes<=64U*1024U&&limits.maxMessagesPerTick>0U&&limits.maxMessagesPerTick<=4096U; }
} // namespace kb::network

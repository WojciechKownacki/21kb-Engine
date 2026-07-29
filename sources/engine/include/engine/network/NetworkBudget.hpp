#pragma once

#include <cstddef>
#include <cstdint>

namespace kb::network {

struct NetworkBudget { std::uint16_t tickRate = 30U; std::size_t packetBytes = 1200U; std::size_t maxQueuedBytes = 64U * 1024U; };
[[nodiscard]] constexpr bool IsValidNetworkBudget(NetworkBudget budget) noexcept { return budget.tickRate>=10U&&budget.tickRate<=240U&&budget.packetBytes>=64U&&budget.packetBytes<=64U*1024U&&budget.maxQueuedBytes>=budget.packetBytes&&budget.maxQueuedBytes<=16U*1024U*1024U; }
[[nodiscard]] constexpr bool AcceptsPacket(NetworkBudget budget, std::size_t queuedBytes, std::size_t packetBytes) noexcept { return IsValidNetworkBudget(budget)&&packetBytes>0U&&packetBytes<=budget.packetBytes&&queuedBytes<=budget.maxQueuedBytes-packetBytes; }
[[nodiscard]] constexpr std::uint64_t TickDurationMicroseconds(NetworkBudget budget) noexcept { return IsValidNetworkBudget(budget)?1'000'000U/budget.tickRate:0U; }

} // namespace kb::network

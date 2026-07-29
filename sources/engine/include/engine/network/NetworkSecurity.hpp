#pragma once

#include "engine/network/NetworkObject.hpp"

#include <cstddef>
#include <cstdint>

namespace kb::network {
struct NetworkSecurityLimits { std::size_t maxPayloadBytes = 1200U; std::uint32_t maxMessagesPerTick = 64U; };
[[nodiscard]] constexpr bool IsValidSecurityLimits(NetworkSecurityLimits limits) noexcept { return limits.maxPayloadBytes>0U&&limits.maxPayloadBytes<=64U*1024U&&limits.maxMessagesPerTick>0U&&limits.maxMessagesPerTick<=4096U; }
[[nodiscard]] inline bool ValidateIncomingMessage(const NetworkObjects& objects, NetworkSecurityLimits limits, NetworkObjectId object, NetworkPeerId sender, std::size_t payloadBytes, std::uint32_t messagesThisTick, std::size_t declaredBytes) noexcept { return IsValidSecurityLimits(limits)&&payloadBytes<=limits.maxPayloadBytes&&declaredBytes==payloadBytes&&messagesThisTick<limits.maxMessagesPerTick&&objects.CanAcceptOwnerCommand(object,sender); }
} // namespace kb::network

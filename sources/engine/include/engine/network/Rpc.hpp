#pragma once

#include "engine/network/NetworkObject.hpp"

#include <cstdint>

namespace kb::network {

enum class RpcReliability : std::uint8_t { Reliable, Unreliable };
enum class RpcDirection : std::uint8_t { ClientToServer, ServerToClient };

struct RpcDescriptor {
    NetworkObjectId object = 0U;
    NetworkPeerId sender = 0U;
    RpcReliability reliability = RpcReliability::Reliable;
    RpcDirection direction = RpcDirection::ClientToServer;
};

[[nodiscard]] bool ValidateRpc(const NetworkObjects& objects, const RpcDescriptor& rpc) noexcept;

} // namespace kb::network

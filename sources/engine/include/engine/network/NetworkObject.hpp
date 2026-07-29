#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

namespace kb::network {

using NetworkObjectId = std::uint64_t;
using NetworkPeerId = std::uint64_t;

enum class NetworkRole : std::uint8_t { None, Authority, Proxy };

struct NetworkObject {
    NetworkObjectId id = 0U;
    NetworkPeerId owner = 0U;
    NetworkRole role = NetworkRole::None;
};

class NetworkObjects final {
public:
    [[nodiscard]] bool Spawn(NetworkObject object);
    [[nodiscard]] bool Despawn(NetworkObjectId id) noexcept;
    [[nodiscard]] bool AssignOwner(NetworkObjectId id, NetworkPeerId owner) noexcept;
    [[nodiscard]] std::optional<NetworkObject> Find(NetworkObjectId id) const noexcept;
    [[nodiscard]] bool CanAcceptOwnerCommand(NetworkObjectId id, NetworkPeerId sender) const noexcept;

private:
    std::unordered_map<NetworkObjectId, NetworkObject> objects_;
};

} // namespace kb::network

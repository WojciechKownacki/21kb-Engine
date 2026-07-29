#pragma once

#include <cstdint>

namespace kb::network {

enum class NetworkModel : std::uint8_t { OfflineOnly, AuthoritativeServer, ListenServer, PeerToPeer };

struct NetworkConfiguration {
    NetworkModel model = NetworkModel::OfflineOnly;
    bool HasTransport() const noexcept { return model != NetworkModel::OfflineOnly; }
};

// First release ships no transport. Multiplayer APIs remain unavailable until
// an authority model, transport and bounded serializer are implemented.
inline constexpr NetworkConfiguration kFirstReleaseNetwork{};

} // namespace kb::network

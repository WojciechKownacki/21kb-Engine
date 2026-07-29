#pragma once

#include "engine/network/NetworkModel.hpp"
#include "engine/network/ReplicationSchema.hpp"

namespace kb::network {
[[nodiscard]] inline bool CanOpenNetworkSession(NetworkConfiguration configuration) noexcept { return configuration.HasTransport(); }
[[nodiscard]] inline bool AreSchemasCompatible(const ReplicationSchema& local, const ReplicationSchema& remote) noexcept { return ValidateReplicationSchema(local)&&ValidateReplicationSchema(remote)&&local.version==remote.version; }
} // namespace kb::network

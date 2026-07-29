#pragma once

#include "engine/network/NetworkModel.hpp"
#include "engine/network/ReplicationSchema.hpp"

namespace kb::network {
[[nodiscard]] inline bool CanOpenNetworkSession(NetworkConfiguration configuration) noexcept { return configuration.HasTransport(); }
[[nodiscard]] inline bool AreSchemasCompatible(const ReplicationSchema& local, const ReplicationSchema& remote) noexcept {
    if (!ValidateReplicationSchema(local) || !ValidateReplicationSchema(remote) ||
        local.version != remote.version || local.fields.size() != remote.fields.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < local.fields.size(); ++index) {
        const ReplicatedField& expected = local.fields[index];
        const ReplicatedField& actual = remote.fields[index];
        if (expected.id != actual.id || expected.type != actual.type ||
            expected.minimum != actual.minimum || expected.maximum != actual.maximum ||
            expected.quantizationBits != actual.quantizationBits) {
            return false;
        }
    }
    return true;
}
} // namespace kb::network

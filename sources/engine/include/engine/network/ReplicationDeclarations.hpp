#pragma once

#include "engine/network/ReplicationAudience.hpp"

#include <string>
#include <vector>

namespace kb::network {

struct ReplicatedFieldDecl {
    std::string component;
    std::string field;
    ReplicationAudience audience = ReplicationAudience::Everyone;
    float minimum = 0.0F, maximum = 0.0F;
    std::uint8_t bits = 0U;
    std::uint32_t maxUInt = 0U;
};

struct ReplicatedVariableDecl {
    enum class Type { Float, Int, Bool };
    std::string name;
    Type type = Type::Float;
    float minimum = 0.0F, maximum = 0.0F;
    std::uint8_t bits = 0U;
    ReplicationAudience audience = ReplicationAudience::Everyone;
};

struct ReplicationDeclarations {
    std::uint16_t version = 1U;
    std::vector<ReplicatedFieldDecl> fields;
    std::vector<ReplicatedVariableDecl> variables;
};

[[nodiscard]] bool ParseReplicationDeclarations(std::string_view json, ReplicationDeclarations& out, std::string& error);
[[nodiscard]] std::string WriteReplicationDeclarations(const ReplicationDeclarations& declarations);

} // namespace kb::network

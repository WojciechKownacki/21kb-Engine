#pragma once

#include "kb/net/Replication.hpp"
#include "engine/network/ReplicationDeclarations.hpp"

namespace kb::net {

[[nodiscard]] std::unique_ptr<ComponentCodec> MakeReflectedFieldsCodec(const kb::ecs::World& world,
    std::vector<kb::network::ReplicatedFieldDecl> fields, bool removeWholeComponent, std::string& error);
[[nodiscard]] std::unique_ptr<ComponentCodec> MakeNetworkedVariablesCodec(const kb::network::ReplicationDeclarations& declarations,
    ReplicationAudience audience, std::string& error);

} // namespace kb::net

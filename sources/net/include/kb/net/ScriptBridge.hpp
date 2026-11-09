#pragma once

#include "engine/network/NetworkScriptBridge.hpp"
#include "kb/net/Replication.hpp"

#include <map>
#include <string>
#include <vector>

namespace kb::scene {
class Scene;
}

// The kb_net side of the script API (Net.*, engine/script/ScriptNetApi.hpp): an INetworkScriptRuntime that
// carries a scene's RPC declarations, replicate requests and calls over a ReplicationServer or a
// ReplicationClient, and turns what happens on the network into script events.
//
// Order of use, on a server and on a client alike:
//   1. construct it for the scene (it attaches itself, so Net.* sees it before any script runs);
//   2. let the scripts start (they call Net.RegisterRpc and, on a server, Net.Replicate);
//   3. Freeze it into the RpcRegistry the session will use, create the session objects with that registry;
//   4. Bind it to them, and pass every NetEvent to HandleEvent.
// Net.Call before Bind answers that the session has not started.
//
// Script events (arguments are read as event.args.<name> in Lua):
//   OnNetConnected(player)            a client connected (server), or this client's connection is up (client)
//   OnNetDisconnected(player, reason) the same for leaving
//   OnNetSpawned(entity, owner)       client: a replicated entity appeared in the world given to ReplicationClient
//   OnNetDespawned(entity)            client: it went away
//   OnRpc_<name>(sender, n1.., s1.., b1.., e1..)   a call arrived; sender is the calling player (0: the server)
// An `entity` on a client is an entity of the replica world, usable as an argument of Net.Call.
namespace kb::net {

class ScriptNetworkRuntime final : public kb::network::INetworkScriptRuntime {
public:
    enum class Role : std::uint8_t { Server, Client };

    ScriptNetworkRuntime(Role role, kb::scene::Scene& scene);
    ~ScriptNetworkRuntime() override;

    // Defines the declared RPCs after the ones already in `registry` (the standard ones). After this, Net.RegisterRpc is refused.
    [[nodiscard]] bool Freeze(RpcRegistry& registry, std::string& error);
    void BindServer(ReplicationServer& server);
    void BindClient(ReplicationClient& replication, Client& connection, std::uint64_t playerId);
    void SetClientAuthorized(bool authorized) noexcept { clientAuthorized_ = authorized; }
    void HandleEvent(const NetEvent& event);

    [[nodiscard]] kb::network::ScriptNetMode Mode() const override { return role_ == Role::Server ? kb::network::ScriptNetMode::Server : kb::network::ScriptNetMode::Client; }
    [[nodiscard]] bool Connected() const override;
    [[nodiscard]] std::uint64_t LocalPlayerId() const override { return role_ == Role::Client ? playerId_ : 0U; }
    [[nodiscard]] std::size_t PlayerCount() const override;
    bool DeclareRpc(const kb::network::ScriptRpcDeclaration& declaration, std::string& error) override;
    [[nodiscard]] const kb::network::ScriptRpcDeclaration* FindRpc(const std::string& name) const override;
    bool CallRpc(const std::string& name, std::uint64_t target, std::span<const kb::network::ScriptRpcValue> args, std::string& error) override;
    bool Replicate(std::uint64_t entity, std::string& error) override;
    [[nodiscard]] bool IsReplicated(std::uint64_t entity) const override;
    bool SetOwner(std::uint64_t entity, std::uint64_t player, std::string& error) override;

private:
    void Deliver(const kb::network::ScriptRpcDeclaration& declaration, std::uint64_t sender, std::span<const RpcValue> args);
    void Queue(std::string name, std::vector<std::pair<std::string, kb::network::ScriptRpcValue>> arguments);

    Role role_;
    kb::scene::Scene* scene_;
    std::vector<kb::network::ScriptRpcDeclaration> declarations_;
    bool frozen_ = false;
    std::vector<std::uint64_t> pendingReplicate_;
    ReplicationServer* server_ = nullptr;
    ReplicationClient* replication_ = nullptr;
    Client* connection_ = nullptr;
    std::uint64_t playerId_ = 0U;
    bool clientAuthorized_ = true;
};

} // namespace kb::net

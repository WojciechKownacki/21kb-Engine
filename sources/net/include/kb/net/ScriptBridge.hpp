#pragma once

#include "engine/network/NetworkScriptBridge.hpp"
#include "kb/net/Replication.hpp"
#include "engine/network/ReplicationDeclarations.hpp"

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
//   OnNetConnected(player)            a client connected (server), or this client's session is open (client; Net.Mode().connected turns true a moment later, when calls work)
//   OnNetDisconnected(player, reason) the same for leaving
//   OnNetSpawned(entity, owner)       client: a replicated entity appeared in the world given to ReplicationClient
//   OnNetDespawned(entity)            client: it went away
//   OnNetAuthExpired()                client: the login token expired and the server ended the session (a new token is needed)
//   OnRpc_<name>(sender, n1.., s1.., b1.., e1..)   a call arrived; sender is the calling player (0: the server)
// An `entity` on a client is an entity of the replica world, usable as an argument of Net.Call.
namespace kb::net {

class ScriptNetworkRuntime final : public kb::network::INetworkScriptRuntime {
public:
    enum class Role : std::uint8_t { Offline, Server, Client };

    ScriptNetworkRuntime(Role role, kb::scene::Scene& scene, kb::network::ReplicationDeclarations declarations);
    ~ScriptNetworkRuntime() override;

    // Defines the declared RPCs after the ones already in `registry` (the standard ones). After this, Net.RegisterRpc is refused.
    [[nodiscard]] bool Freeze(RpcRegistry& registry, std::string& error);
    void BindServer(ReplicationServer& server);
    void BindClient(ReplicationClient& replication, Client& connection);
    void SetClientAuthorized(bool authorized) noexcept { clientAuthorized_ = authorized; }
    void NotifyAuthExpired() { Queue("OnNetAuthExpired", {}); }
    void HandleEvent(const NetEvent& event);

    [[nodiscard]] kb::network::ScriptNetMode Mode() const override {
        switch (role_) {
        case Role::Offline: return kb::network::ScriptNetMode::Offline;
        case Role::Server: return kb::network::ScriptNetMode::Server;
        case Role::Client: return kb::network::ScriptNetMode::Client;
        }
        return kb::network::ScriptNetMode::Offline;
    }
    [[nodiscard]] bool Connected() const override;
    [[nodiscard]] std::uint64_t LocalPlayerId() const override { return role_ == Role::Client && connection_ != nullptr ? connection_->AssignedId() : 0U; }
    [[nodiscard]] std::size_t PlayerCount() const override;
    bool DeclareRpc(const kb::network::ScriptRpcDeclaration& declaration, std::string& error) override;
    [[nodiscard]] const kb::network::ScriptRpcDeclaration* FindRpc(const std::string& name) const override;
    bool CallRpc(const std::string& name, std::uint64_t target, std::span<const kb::network::ScriptRpcValue> args, std::string& error) override;
    bool Replicate(std::uint64_t entity, std::string& error, bool global = false) override;
    [[nodiscard]] bool IsReplicated(std::uint64_t entity) const override;
    bool SetOwner(std::uint64_t entity, std::uint64_t player, std::string& error) override;
    void ComponentChanged(std::uint64_t entity, bool appeared = false) override;
    bool SetVar(std::uint64_t entity, const std::string& name, double value, std::string& error) override;
    bool GetVar(std::uint64_t entity, const std::string& name, double& value, std::string& error) const override;
    void SetTransformAppeared(std::function<void(kb::ecs::Entity)> callback) { transformAppeared_ = std::move(callback); }

    // The role may change only while no session is bound (offline <-> client): the RPC declarations and the replication declarations stay.
    [[nodiscard]] bool SetRole(Role role) noexcept;
    void UnbindClient() noexcept { replication_ = nullptr; connection_ = nullptr; clientAuthorized_ = false; }
    [[nodiscard]] const kb::network::ReplicationDeclarations& Declarations() const noexcept { return replicationDeclarations_; }
    using JoinHandler = std::function<bool(const kb::network::ScriptJoinRequest&, std::string&)>;
    void SetSessionHandlers(JoinHandler join, std::function<void()> leave) { join_ = std::move(join); leave_ = std::move(leave); }
    bool RequestJoin(const kb::network::ScriptJoinRequest& request, std::string& error) override;
    void RequestLeave() override;

private:
    void Deliver(const kb::network::ScriptRpcDeclaration& declaration, std::uint64_t sender, std::span<const RpcValue> args);
    void Queue(std::string name, std::vector<std::pair<std::string, kb::network::ScriptRpcValue>> arguments);
    void PrepareVariables(kb::ecs::Entity entity);
    void VariablesChanged(NetworkId id, kb::ecs::Entity entity, bool initial);
    [[nodiscard]] bool VariableVisible(const kb::network::ReplicatedVariableDecl& variable, NetworkId id) const;

    Role role_;
    kb::scene::Scene* scene_;
    std::vector<kb::network::ScriptRpcDeclaration> declarations_;
    bool frozen_ = false;
    struct PendingReplication { std::uint64_t entity; bool global; };
    std::vector<PendingReplication> pendingReplicate_;
    ReplicationServer* server_ = nullptr;
    ReplicationClient* replication_ = nullptr;
    Client* connection_ = nullptr;
    bool clientAuthorized_ = true;
    std::function<void(kb::ecs::Entity)> transformAppeared_;
    JoinHandler join_;
    std::function<void()> leave_;
    kb::network::ReplicationDeclarations replicationDeclarations_;
    std::string replicationError_;
    struct VariableSnapshot { std::array<float, 32U> values{}; std::uint32_t present = 0U; };
    std::map<NetworkId, VariableSnapshot> variableSnapshots_;
};

} // namespace kb::net

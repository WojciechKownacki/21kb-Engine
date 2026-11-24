#include "kb/net/ScriptBridge.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptNetApi.hpp"
#include "kb/net/SceneReplication.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/NetworkedVariablesComponent.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace kb::net {

namespace {

using kb::network::ScriptRpcArgKind;
using kb::network::ScriptRpcArgSpec;
using kb::network::ScriptRpcDeclaration;
using kb::network::ScriptRpcValue;

[[nodiscard]] RpcDefinition ToDefinition(const ScriptRpcDeclaration& declaration) {
    RpcDefinition definition;
    definition.name = declaration.name;
    definition.direction = declaration.toServer ? RpcDirection::ClientToServer : RpcDirection::ServerToClient;
    definition.reliable = declaration.reliable;
    definition.ownerArg = declaration.ownerArg;
    definition.maxCallsPerSecond = declaration.maxCallsPerSecond;
    for (const ScriptRpcArgSpec& spec : declaration.args) {
        RpcArgSpec arg;
        switch (spec.kind) {
        case ScriptRpcArgKind::Bool: arg.type = RpcArgType::Bool; break;
        case ScriptRpcArgKind::Int: arg.type = RpcArgType::Int32; break;
        case ScriptRpcArgKind::UInt: arg.type = RpcArgType::UInt32; break;
        case ScriptRpcArgKind::Float: arg.type = RpcArgType::Float; break;
        case ScriptRpcArgKind::String: arg.type = RpcArgType::String; break;
        case ScriptRpcArgKind::Entity: arg.type = RpcArgType::NetworkId; break;
        }
        arg.min = spec.minimum;
        arg.max = spec.maximum;
        arg.maxLength = spec.maxLength;
        definition.args.push_back(arg);
    }
    return definition;
}

[[nodiscard]] const char* ReasonText(DisconnectReason reason) noexcept {
    switch (reason) {
    case DisconnectReason::Requested: return "requested";
    case DisconnectReason::PeerClosed: return "peer-closed";
    case DisconnectReason::Timeout: return "timeout";
    case DisconnectReason::ConnectTimeout: return "connect-timeout";
    case DisconnectReason::Replaced: return "replaced";
    case DisconnectReason::Error: return "error";
    case DisconnectReason::VersionMismatch: return "version-mismatch";
    case DisconnectReason::ContentMismatch: return "content-mismatch";
    case DisconnectReason::OutdatedClient: return "outdated-client";
    case DisconnectReason::ServerFull: return "server-full";
    case DisconnectReason::PasswordRejected: return "password-rejected";
    case DisconnectReason::None: break;
    }
    return "none";
}

[[nodiscard]] const char* StatusText(RpcStatus status) noexcept {
    switch (status) {
    case RpcStatus::Ok: return "ok";
    case RpcStatus::UnknownRpc: return "the RPC is unknown to the session";
    case RpcStatus::WrongDirection: return "the RPC goes the other way";
    case RpcStatus::BadArguments: return "the arguments were refused";
    case RpcStatus::Malformed: return "the call could not be encoded";
    case RpcStatus::TooLarge: return "the call is too large";
    case RpcStatus::Busy: return "the send queue is full, try again later";
    case RpcStatus::NotReady: return "the connection is not ready yet";
    case RpcStatus::RateLimited: return "too many calls per second";
    case RpcStatus::PermissionDenied: return "the caller does not own the entity the call names";
    case RpcStatus::NoHandler: return "the server has no handler for this call";
    }
    return "failed";
}

} // namespace

ScriptNetworkRuntime::ScriptNetworkRuntime(Role role, kb::scene::Scene& scene, kb::network::ReplicationDeclarations declarations)
    : role_(role), scene_(&scene), replicationDeclarations_(std::move(declarations)) {
    static_cast<void>(BuildReplicationRegistry(scene.Runtime().EcsWorld(), replicationDeclarations_, 1000.0F, replicationError_));
    kb::script::ScriptNetApi::Attach(scene, this);
}

ScriptNetworkRuntime::~ScriptNetworkRuntime() {
    kb::script::ScriptNetApi::Attach(*scene_, nullptr);
}

bool ScriptNetworkRuntime::Connected() const {
    if (role_ == Role::Offline) {
        return false;
    }
    if (ServesScene()) {
        return server_ != nullptr;
    }
    // Up means the player can call the server: the session is open and the server has agreed on the declarations (OnNetConnected comes a moment earlier).
    return clientAuthorized_ && connection_ != nullptr && connection_->State() == ClientState::Connected && replication_ != nullptr && replication_->Ready();
}

std::size_t ScriptNetworkRuntime::PlayerCount() const {
    if (ServesScene()) {
        return (server_ == nullptr ? 0U : server_->ReadyClients()) + (role_ == Role::Host ? 1U : 0U);
    }
    return Connected() ? 1U : 0U;
}

bool ScriptNetworkRuntime::DeclareRpc(const ScriptRpcDeclaration& declaration, std::string& error) {
    if (frozen_) {
        error = "the session has started; declare RPCs in Created or Ready";
        return false;
    }
    if (FindRpc(declaration.name) != nullptr) {
        error = "already registered";
        return false;
    }
    RpcRegistry probe;
    if (!probe.Define(ToDefinition(declaration), error)) {
        return false;
    }
    declarations_.push_back(declaration);
    return true;
}

const ScriptRpcDeclaration* ScriptNetworkRuntime::FindRpc(const std::string& name) const {
    const auto found = std::ranges::find_if(declarations_, [&name](const ScriptRpcDeclaration& declaration) { return declaration.name == name; });
    return found == declarations_.end() ? nullptr : &*found;
}

bool ScriptNetworkRuntime::Freeze(RpcRegistry& registry, std::string& error) {
    if (!replicationError_.empty()) { error = replicationError_; return false; }
    for (const ScriptRpcDeclaration& declaration : declarations_) {
        if (!registry.Define(ToDefinition(declaration), error)) {
            error = "RPC '" + declaration.name + "': " + error;
            return false;
        }
    }
    frozen_ = true;
    return true;
}

bool ScriptNetworkRuntime::SetRole(Role role) noexcept {
    if (server_ != nullptr || connection_ != nullptr) {
        return false;
    }
    role_ = role;
    return true;
}

bool ScriptNetworkRuntime::RequestJoin(const kb::network::ScriptJoinRequest& request, std::string& error) {
    if (role_ != Role::Offline) {
        error = "already in a session";
        return false;
    }
    if (!join_) {
        error = "this scene has no network";
        return false;
    }
    return join_(request, error);
}

void ScriptNetworkRuntime::RequestLeave() {
    if (leave_) {
        leave_();
    }
}

void ScriptNetworkRuntime::Queue(std::string name, std::vector<std::pair<std::string, ScriptRpcValue>> arguments) {
    kb::script::ScriptNetApi::QueueEvent(*scene_, kb::script::ScriptNetEvent{ std::move(name), std::move(arguments) });
}

void ScriptNetworkRuntime::BindServer(ReplicationServer& server) {
    server_ = &server;
    for (std::size_t index = 0U; index < declarations_.size(); ++index) {
        if (declarations_[index].toServer) {
            server.SetRpcHandler(declarations_[index].name, [this, index](std::uint64_t caller, std::span<const RpcValue> args) {
                Deliver(declarations_[index], caller, args);
            });
        }
    }
    for (const auto& request : pendingReplicate_) {
        static_cast<void>(server.Replicate(kb::ecs::Entity{ request.entity }, 0U));
        static_cast<void>(server.SetAlwaysRelevant(server.NetworkIdOf(kb::ecs::Entity{ request.entity }), request.global));
    }
    pendingReplicate_.clear();
    if (role_ == Role::Host) {
        Queue("OnNetConnected", { { "player", static_cast<std::uint32_t>(kHostPlayerId) } });
    }
}

void ScriptNetworkRuntime::BindClient(ReplicationClient& replication, Client& connection) {
    replication_ = &replication;
    connection_ = &connection;
    variableSnapshots_.clear();
    replication.SetStateCallback([this](NetworkId id, kb::ecs::Entity entity, std::uint32_t) { VariablesChanged(id, entity, false); });
    for (std::size_t index = 0U; index < declarations_.size(); ++index) {
        if (!declarations_[index].toServer) {
            replication.SetRpcHandler(declarations_[index].name, [this, index](std::span<const RpcValue> args) { Deliver(declarations_[index], 0U, args); });
        }
    }
    replication.SetReplicaCallbacks(
        [this](NetworkId id, kb::ecs::Entity entity, std::uint64_t owner) {
            VariablesChanged(id, entity, true);
            Queue("OnNetSpawned", { { "entity", entity.Id() }, { "owner", static_cast<std::uint32_t>(owner) } });
        },
        [this](NetworkId id, kb::ecs::Entity entity) { variableSnapshots_.erase(id); Queue("OnNetDespawned", { { "entity", entity.Id() } }); });
}

void ScriptNetworkRuntime::HandleEvent(const NetEvent& event) {
    if (event.type == NetEventType::Connected) {
        Queue("OnNetConnected", { { "player", static_cast<std::uint32_t>(ServesScene() ? event.peer : LocalPlayerId()) } });
    } else if (event.type == NetEventType::Disconnected) {
        Queue("OnNetDisconnected", { { "player", static_cast<std::uint32_t>(ServesScene() ? event.peer : LocalPlayerId()) }, { "reason", std::string{ ReasonText(event.reason) } } });
    }
}

void ScriptNetworkRuntime::Deliver(const ScriptRpcDeclaration& declaration, std::uint64_t sender, std::span<const RpcValue> args) {
    if (args.size() != declaration.args.size()) {
        return;
    }
    std::vector<std::pair<std::string, ScriptRpcValue>> arguments;
    arguments.emplace_back("sender", static_cast<std::uint32_t>(sender));
    std::array<std::size_t, 4> next{}; // bool, string, entity, number
    for (std::size_t index = 0U; index < args.size(); ++index) {
        const ScriptRpcArgKind kind = declaration.args[index].kind;
        switch (kind) {
        case ScriptRpcArgKind::Bool:
            arguments.emplace_back("b" + std::to_string(++next[0]), std::get<bool>(args[index]));
            break;
        case ScriptRpcArgKind::String:
            arguments.emplace_back("s" + std::to_string(++next[1]), std::get<std::string>(args[index]));
            break;
        case ScriptRpcArgKind::Entity: {
            const NetworkId id = std::get<NetworkId>(args[index]);
            const kb::ecs::Entity entity = ServesScene() ? (server_ != nullptr ? server_->EntityOf(id) : kb::ecs::Entity{})
                                                                  : (replication_ != nullptr ? replication_->Find(id) : kb::ecs::Entity{});
            if (!entity.IsValid()) {
                return; // names an entity this side does not have (any more): the call is dropped
            }
            arguments.emplace_back("e" + std::to_string(++next[2]), entity.Id());
            break;
        }
        case ScriptRpcArgKind::Int:
            arguments.emplace_back("n" + std::to_string(++next[3]), std::get<std::int32_t>(args[index]));
            break;
        case ScriptRpcArgKind::UInt:
            arguments.emplace_back("n" + std::to_string(++next[3]), std::get<std::uint32_t>(args[index]));
            break;
        case ScriptRpcArgKind::Float:
            arguments.emplace_back("n" + std::to_string(++next[3]), std::get<float>(args[index]));
            break;
        }
    }
    Queue("OnRpc_" + declaration.name, std::move(arguments));
}

bool ScriptNetworkRuntime::CallRpc(const std::string& name, std::uint64_t target, std::span<const ScriptRpcValue> args, std::string& error) {
    const ScriptRpcDeclaration* declaration = FindRpc(name);
    if (declaration == nullptr) {
        error = "no such RPC";
        return false;
    }
    if (role_ == Role::Host) {
        if (server_ == nullptr) {
            error = "the session has not started (declare RPCs in Ready and call them from Tick or an event)";
            return false;
        }
        if (declaration->toServer && target != 0U) {
            error = "a player calls the server only (target 0)";
            return false;
        }
        std::vector<RpcValue> wire;
        wire.reserve(args.size());
        for (const ScriptRpcValue& value : args) {
            if (const auto* flag = std::get_if<bool>(&value)) {
                wire.emplace_back(*flag);
            } else if (const auto* number = std::get_if<std::int32_t>(&value)) {
                wire.emplace_back(*number);
            } else if (const auto* unsignedNumber = std::get_if<std::uint32_t>(&value)) {
                wire.emplace_back(*unsignedNumber);
            } else if (const auto* real = std::get_if<float>(&value)) {
                wire.emplace_back(*real);
            } else if (const auto* text = std::get_if<std::string>(&value)) {
                wire.emplace_back(*text);
            } else {
                const kb::ecs::Entity entity{ std::get<std::uint64_t>(value) };
                const NetworkId id = server_->NetworkIdOf(entity);
                if (id == 0U) {
                    error = "an entity argument is not replicated (the server marks an object with Net.Replicate; a client passes the replica it got in OnNetSpawned, not its own scene object)";
                    return false;
                }
                wire.emplace_back(id);
            }
        }
        RpcStatus status = RpcStatus::Ok;
        if (declaration->toServer) {
            status = server_->CallLocalRpc(kHostPlayerId, name, wire, nowMilliseconds_);
        } else {
            if (target == 0U) {
                server_->CallRpcAll(name, wire, status);
            } else if (target != kHostPlayerId) {
                status = server_->CallRpc(target, name, wire);
            }
            if (target == 0U || target == kHostPlayerId) {
                Deliver(*declaration, 0U, wire);
            }
        }
        if (status != RpcStatus::Ok) {
            error = StatusText(status);
            return false;
        }
        return true;
    }
    if (role_ == Role::Server ? declaration->toServer : !declaration->toServer) {
        error = role_ == Role::Server ? "this RPC goes from clients to the server" : "this RPC goes from the server to clients";
        return false;
    }
    if (role_ == Role::Server && server_ == nullptr) {
        error = "the session has not started (declare RPCs in Ready and call them from Tick or an event)";
        return false;
    }
    if (role_ == Role::Client && (!clientAuthorized_ || replication_ == nullptr || !replication_->Ready())) {
        error = "the connection is not ready (call the server once Net.Mode().connected is true: OnNetConnected comes a moment before; not while the client reconnects)";
        return false;
    }
    if (role_ == Role::Client && target != 0U) {
        error = "a client calls the server only (target 0)";
        return false;
    }
    std::vector<RpcValue> wire;
    wire.reserve(args.size());
    for (const ScriptRpcValue& value : args) {
        if (const auto* flag = std::get_if<bool>(&value)) {
            wire.emplace_back(*flag);
        } else if (const auto* number = std::get_if<std::int32_t>(&value)) {
            wire.emplace_back(*number);
        } else if (const auto* unsignedNumber = std::get_if<std::uint32_t>(&value)) {
            wire.emplace_back(*unsignedNumber);
        } else if (const auto* real = std::get_if<float>(&value)) {
            wire.emplace_back(*real);
        } else if (const auto* text = std::get_if<std::string>(&value)) {
            wire.emplace_back(*text);
        } else {
            const kb::ecs::Entity entity{ std::get<std::uint64_t>(value) };
            const NetworkId id = ServesScene() ? server_->NetworkIdOf(entity) : replication_->NetworkIdOf(entity);
            if (id == 0U) {
                error = "an entity argument is not replicated (the server marks an object with Net.Replicate; a client passes the replica it got in OnNetSpawned, not its own scene object)";
                return false;
            }
            wire.emplace_back(id);
        }
    }
    RpcStatus status = RpcStatus::Ok;
    if (role_ == Role::Client) {
        status = replication_->CallRpc(name, wire);
    } else if (target == 0U) {
        server_->CallRpcAll(name, wire, status);
    } else {
        status = server_->CallRpc(target, name, wire);
    }
    if (status != RpcStatus::Ok) {
        error = StatusText(status);
        return false;
    }
    return true;
}

void ScriptNetworkRuntime::ComponentChanged(std::uint64_t entity, bool appeared) {
    if (server_ == nullptr) return;
    server_->MarkDirty(kb::ecs::Entity{entity});
    if (appeared && transformAppeared_) transformAppeared_(kb::ecs::Entity{entity});
}

void ScriptNetworkRuntime::PrepareVariables(kb::ecs::Entity entity) {
    auto& world = scene_->Runtime().EcsWorld();
    if (replicationDeclarations_.variables.empty() || world.Has<kb::scene::NetworkedVariablesComponent>(entity)) return;
    kb::scene::NetworkedVariablesComponent component;
    for (std::size_t i = 0U; i < replicationDeclarations_.variables.size(); ++i) {
        const auto& variable = replicationDeclarations_.variables[i];
        component.values[i] = std::clamp(0.0F, variable.minimum, variable.maximum);
    }
    world.Set(entity, component);
    ComponentChanged(entity.Id());
}

bool ScriptNetworkRuntime::VariableVisible(const kb::network::ReplicatedVariableDecl& variable, NetworkId id) const {
    if (ServesScene()) return true;
    if (replication_ == nullptr || id == 0U) return false;
    const auto owner = replication_->OwnerOf(id);
    return variable.audience == ReplicationAudience::Everyone || variable.audience == ReplicationAudience::InitialOnly ||
        (variable.audience == ReplicationAudience::Owner && owner == LocalPlayerId()) ||
        (variable.audience == ReplicationAudience::Others && owner != LocalPlayerId());
}

bool ScriptNetworkRuntime::SetVar(std::uint64_t entity, const std::string& name, double value, std::string& error) {
    if (!ServesScene()) { error = name + ": only a server can set variables"; return false; }
    if (!replicationError_.empty()) { error = replicationError_; return false; }
    const auto found = std::ranges::find(replicationDeclarations_.variables, name, &kb::network::ReplicatedVariableDecl::name);
    if (found == replicationDeclarations_.variables.end()) { error = name + ": unknown variable"; return false; }
    const auto id = kb::ecs::Entity{entity};
    auto& world = scene_->Runtime().EcsWorld();
    const auto* existing = id.IsValid() && world.IsAlive(id) ? world.TryGet<kb::scene::NetworkedVariablesComponent>(id) : nullptr;
    if (existing == nullptr) { error = name + ": missing NetworkedVariables component"; return false; }
    if (!std::isfinite(value) || value < found->minimum || value > found->maximum ||
        (found->type != kb::network::ReplicatedVariableDecl::Type::Float && value != std::floor(value))) {
        error = name + ": value outside the declared range"; return false;
    }
    auto component = *existing;
    component.values[static_cast<std::size_t>(found - replicationDeclarations_.variables.begin())] = static_cast<float>(value);
    world.Set(id, component);
    ComponentChanged(entity);
    error.clear();
    return true;
}

bool ScriptNetworkRuntime::GetVar(std::uint64_t entity, const std::string& name, double& value, std::string& error) const {
    if (!replicationError_.empty()) { error = replicationError_; return false; }
    const auto found = std::ranges::find(replicationDeclarations_.variables, name, &kb::network::ReplicatedVariableDecl::name);
    if (found == replicationDeclarations_.variables.end()) { error = name + ": unknown variable"; return false; }
    const auto id = kb::ecs::Entity{entity};
    const auto& world = role_ == Role::Client && replication_ != nullptr ? replication_->ReplicaWorld() : scene_->Runtime().EcsWorld();
    if (!id.IsValid() || !world.IsAlive(id) || (role_ == Role::Client && !VariableVisible(*found, replication_ == nullptr ? 0U : replication_->NetworkIdOf(id)))) {
        error = name + ": variable is not available to this client"; return false;
    }
    const auto* component = world.TryGet<kb::scene::NetworkedVariablesComponent>(id);
    if (component == nullptr) { error = name + ": missing NetworkedVariables component"; return false; }
    value = component->values[static_cast<std::size_t>(found - replicationDeclarations_.variables.begin())];
    error.clear();
    return true;
}

void ScriptNetworkRuntime::VariablesChanged(NetworkId id, kb::ecs::Entity entity, bool initial) {
    if (replicationDeclarations_.variables.empty()) return;
    const auto* component = replication_->ReplicaWorld().TryGet<kb::scene::NetworkedVariablesComponent>(entity);
    auto& snapshot = variableSnapshots_[id];
    for (std::size_t i = 0U; i < replicationDeclarations_.variables.size(); ++i) {
        const auto& variable = replicationDeclarations_.variables[i];
        const auto bit = 1U << i;
        if (component == nullptr || !VariableVisible(variable, id)) { snapshot.present &= ~bit; continue; }
        const auto value = component->values[i];
        if (!initial && ((snapshot.present & bit) == 0U || snapshot.values[i] != value)) {
            Queue("OnNetVarChanged", {{"entity", entity.Id()}, {"name", variable.name}, {"value", value}});
        }
        snapshot.values[i] = value;
        snapshot.present |= bit;
    }
}

bool ScriptNetworkRuntime::Replicate(std::uint64_t entity, std::string& error, bool global) {
    if (!ServesScene()) {
        error = "only a server replicates";
        return false;
    }
    if (!replicationError_.empty()) { error = replicationError_; return false; }
    if (!replicationDeclarations_.variables.empty()) {
        if (!scene_->Runtime().EcsWorld().IsAlive(kb::ecs::Entity{entity})) { error = "the entity is not alive"; return false; }
        PrepareVariables(kb::ecs::Entity{entity});
    }
    if (server_ == nullptr) {
        const auto found = std::ranges::find(pendingReplicate_, entity, &PendingReplication::entity);
        if (found == pendingReplicate_.end()) pendingReplicate_.push_back({entity, global});
        else found->global = global;
        return true;
    }
    const auto result = server_->Replicate(kb::ecs::Entity{ entity }, 0U);
    if (result == ReplicateResult::Ok || result == ReplicateResult::AlreadyReplicated) {
        return server_->SetAlwaysRelevant(server_->NetworkIdOf(kb::ecs::Entity{entity}), global);
    }
    error = "the entity is not alive or the replication limit is reached";
    return false;
}

bool ScriptNetworkRuntime::IsReplicated(std::uint64_t entity) const {
    if (!ServesScene()) {
        return false;
    }
    if (server_ == nullptr) {
        return std::ranges::find(pendingReplicate_, entity, &PendingReplication::entity) != pendingReplicate_.end();
    }
    return server_->NetworkIdOf(kb::ecs::Entity{ entity }) != 0U;
}

bool ScriptNetworkRuntime::SetOwner(std::uint64_t entity, std::uint64_t player, std::string& error) {
    if (server_ == nullptr) {
        error = "the session has not started";
        return false;
    }
    const NetworkId id = server_->NetworkIdOf(kb::ecs::Entity{ entity });
    if (id == 0U) {
        error = "the entity is not replicated";
        return false;
    }
    if (!server_->SetOwner(id, player)) {
        error = "the owner could not be changed";
        return false;
    }
    return true;
}

} // namespace kb::net

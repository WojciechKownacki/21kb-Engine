#include "kb/net/ScriptBridge.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/script/ScriptNetApi.hpp"

#include <algorithm>
#include <array>

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
    }
    return "failed";
}

} // namespace

ScriptNetworkRuntime::ScriptNetworkRuntime(Role role, kb::scene::Scene& scene) : role_(role), scene_(&scene) {
    kb::script::ScriptNetApi::Attach(scene, this);
}

ScriptNetworkRuntime::~ScriptNetworkRuntime() {
    kb::script::ScriptNetApi::Attach(*scene_, nullptr);
}

bool ScriptNetworkRuntime::Connected() const {
    if (role_ == Role::Server) {
        return server_ != nullptr;
    }
    return connection_ != nullptr && connection_->State() == ClientState::Connected;
}

std::size_t ScriptNetworkRuntime::PlayerCount() const {
    if (role_ == Role::Server) {
        return server_ == nullptr ? 0U : server_->ReadyClients();
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
    for (const ScriptRpcDeclaration& declaration : declarations_) {
        if (!registry.Define(ToDefinition(declaration), error)) {
            error = "RPC '" + declaration.name + "': " + error;
            return false;
        }
    }
    frozen_ = true;
    return true;
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
    for (const std::uint64_t entity : pendingReplicate_) {
        static_cast<void>(server.Replicate(kb::ecs::Entity{ entity }, 0U));
    }
    pendingReplicate_.clear();
}

void ScriptNetworkRuntime::BindClient(ReplicationClient& replication, Client& connection, std::uint64_t playerId) {
    replication_ = &replication;
    connection_ = &connection;
    playerId_ = playerId;
    for (std::size_t index = 0U; index < declarations_.size(); ++index) {
        if (!declarations_[index].toServer) {
            replication.SetRpcHandler(declarations_[index].name, [this, index](std::span<const RpcValue> args) { Deliver(declarations_[index], 0U, args); });
        }
    }
    replication.SetReplicaCallbacks(
        [this](NetworkId, kb::ecs::Entity entity, std::uint64_t owner) {
            Queue("OnNetSpawned", { { "entity", entity.Id() }, { "owner", static_cast<std::uint32_t>(owner) } });
        },
        [this](NetworkId, kb::ecs::Entity entity) { Queue("OnNetDespawned", { { "entity", entity.Id() } }); });
}

void ScriptNetworkRuntime::HandleEvent(const NetEvent& event) {
    if (event.type == NetEventType::Connected) {
        Queue("OnNetConnected", { { "player", static_cast<std::uint32_t>(role_ == Role::Server ? event.peer : playerId_) } });
    } else if (event.type == NetEventType::Disconnected) {
        Queue("OnNetDisconnected", { { "player", static_cast<std::uint32_t>(role_ == Role::Server ? event.peer : playerId_) }, { "reason", std::string{ ReasonText(event.reason) } } });
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
            const NetworkId id = std::get<std::uint32_t>(args[index]);
            const kb::ecs::Entity entity = role_ == Role::Server ? (server_ != nullptr ? server_->EntityOf(id) : kb::ecs::Entity{})
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
    if (role_ == Role::Server ? declaration->toServer : !declaration->toServer) {
        error = role_ == Role::Server ? "this RPC goes from clients to the server" : "this RPC goes from the server to clients";
        return false;
    }
    if (role_ == Role::Server && server_ == nullptr) {
        error = "the session has not started";
        return false;
    }
    if (role_ == Role::Client && (replication_ == nullptr || !replication_->Ready())) {
        error = "the connection is not ready";
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
            const NetworkId id = role_ == Role::Server ? server_->NetworkIdOf(entity) : replication_->NetworkIdOf(entity);
            if (id == 0U) {
                error = "an entity argument is not replicated";
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

bool ScriptNetworkRuntime::Replicate(std::uint64_t entity, std::string& error) {
    if (role_ != Role::Server) {
        error = "only a server replicates";
        return false;
    }
    if (server_ == nullptr) {
        if (std::ranges::find(pendingReplicate_, entity) == pendingReplicate_.end()) {
            pendingReplicate_.push_back(entity); // applied when the session starts
        }
        return true;
    }
    if (server_->NetworkIdOf(kb::ecs::Entity{ entity }) != 0U) {
        return true;
    }
    if (server_->Replicate(kb::ecs::Entity{ entity }, 0U) == 0U) {
        error = "the entity is not alive or the replication limit is reached";
        return false;
    }
    return true;
}

bool ScriptNetworkRuntime::IsReplicated(std::uint64_t entity) const {
    if (role_ != Role::Server) {
        return false;
    }
    if (server_ == nullptr) {
        return std::ranges::find(pendingReplicate_, entity) != pendingReplicate_.end();
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

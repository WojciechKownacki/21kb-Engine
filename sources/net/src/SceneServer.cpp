#include "kb/net/SceneServer.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <ostream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kb::net {

struct SceneServer::Impl {
    Impl(kb::scene::Scene& serverScene, ScriptNetworkRuntime& runtime,
        const kb::network::ReplicationDeclarations& replicationDeclarations, const IKeyProvider& keyProvider,
        std::unique_ptr<IDatagramTransport> networkTransport, SceneServerOptions sceneServerOptions, Log logger)
        : scene(&serverScene), net(&runtime), declarations(replicationDeclarations), keys(&keyProvider),
          transport(std::move(networkTransport)), options(std::move(sceneServerOptions)), sink(std::move(logger)) {}

    void LogMessage(const std::string& message) const {
        if (sink) {
            sink(message);
        }
    }

    void SyncReplicatedEntities() {
        if (options.replicate == ReplicateMode::None) {
            return;
        }
        for (const kb::ecs::Entity entity : candidates) {
            if (replication->IsReplicated(entity)) {
                continue;
            }
            if (options.replicate == ReplicateMode::Prefix) {
                const std::string name{ scene->Entities().Name(entity) };
                if (name.rfind(options.replicatePrefix, 0U) != 0U) {
                    continue;
                }
            }
            std::string error;
            if (!net->Replicate(entity.Id(), error)) LogMessage("replication: " + error);
        }
        candidates.clear();
    }

    void HandleHostEvent(const NetEvent& event, std::uint64_t now) {
        switch (event.type) {
        case NetEventType::Connected:
            LogMessage("client " + std::to_string(event.peer) + " connected");
            break;
        case NetEventType::Disconnected:
            LogMessage("client " + std::to_string(event.peer) + " disconnected");
            break;
        case NetEventType::Message:
            break;
        }
        net->HandleEvent(event);
        replication->HandleEvent(event, now);
    }

    void RemoveAuthorizedPeer(std::uint64_t peer, DisconnectReason reason) {
        if (authorizedPeers.erase(peer) != 0U) {
            HandleHostEvent(NetEvent{ .type = NetEventType::Disconnected, .peer = peer, .reason = reason }, lastNow);
        }
    }

    bool ReloadRevocations() {
        if (options.authRevocationsFile.empty()) return true;
        constexpr std::size_t limit = 1024U * 1024U;
        std::ifstream input(options.authRevocationsFile, std::ios::binary);
        std::vector<std::uint8_t> bytes(limit + 1U);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(input.gcount()));
        const bool valid = !input.bad() && bytes.size() <= limit && auth->UpdateRevocations(bytes);
        if (!valid && !revocationErrorReported) LogMessage("authorization: revocation snapshot rejected; retaining the last valid snapshot");
        revocationErrorReported = !valid;
        return valid;
    }

    void Pump(std::uint64_t now) {
        lastNow = now;
        host->Update(now);
        if (auth != nullptr) {
            if (now >= nextRevocationReload) {
                static_cast<void>(ReloadRevocations());
                nextRevocationReload = now + 1000U;
            }
            std::vector<NetEvent> commands;
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            auth->Update(now, seconds, commands);
            for (const NetEvent& command : commands) {
                if (auth->Authorize(command.peer, kScopePlay)) HandleHostEvent(command, now);
            }
            return;
        }
        NetEvent event;
        while (host->PollEvent(event)) {
            HandleHostEvent(event, now);
        }
    }

    kb::scene::Scene* scene;
    ScriptNetworkRuntime* net;
    const kb::network::ReplicationDeclarations& declarations;
    const IKeyProvider* keys;
    std::unique_ptr<IDatagramTransport> transport;
    SceneServerOptions options;
    Log sink;
    std::unique_ptr<ReplicationRegistry> components;
    std::unique_ptr<RpcRegistry> rpcs;
    std::unique_ptr<Host> host;
    std::unique_ptr<AuthGate> auth;
    std::unordered_set<std::uint64_t> authorizedPeers;
    std::uint64_t nextRevocationReload = 0U;
    bool revocationErrorReported = false;
    std::unique_ptr<ReplicationServer> replication;
    bool profileMetrics = false;
    std::vector<kb::ecs::Entity> candidates;
    std::uint64_t lastNow = 0U;
};

SceneServer::SceneServer(kb::scene::Scene& scene, ScriptNetworkRuntime& net,
    const kb::network::ReplicationDeclarations& declarations, const IKeyProvider& keys,
    std::unique_ptr<IDatagramTransport> transport, SceneServerOptions options, Log log)
    : impl_(std::make_unique<Impl>(scene, net, declarations, keys, std::move(transport), std::move(options), std::move(log))) {}

SceneServer::~SceneServer() = default;

Host& SceneServer::NetHost() noexcept {
    return *impl_->host;
}

ReplicationServer& SceneServer::Replication() noexcept {
    return *impl_->replication;
}

IDatagramTransport& SceneServer::Transport() noexcept {
    return *impl_->transport;
}

AuthGate* SceneServer::Authorization() noexcept {
    return impl_->auth.get();
}

std::size_t SceneServer::AuthorizedPeers() const noexcept {
    return impl_->authorizedPeers.size();
}

bool SceneServer::ProfileMetrics() const noexcept {
    return impl_->profileMetrics;
}

bool SceneServer::Start(std::string& error) {
    Impl& s = *impl_;
    const SceneServerOptions& options = s.options;

    s.components = BuildReplicationRegistry(s.scene->Runtime().EcsWorld(), s.declarations, options.worldExtent, error);
    if (s.components == nullptr) {
        return false;
    }
    s.rpcs = std::make_unique<RpcRegistry>();
    DefineStandardRpcs(*s.rpcs);
    if (!s.net->Freeze(*s.rpcs, error)) {
        return false;
    }
    HostConfig hostConfig;
    hostConfig.identity = options.identity;
    hostConfig.admission = options.admission;
    hostConfig.joinPassword = options.joinPassword;
    hostConfig.maxClients = options.maxClients;
    hostConfig.maxConnectsPerSecondPerAddress = options.maxConnectsPerSecondPerAddress;
    hostConfig.maxSessionsPerAddress = options.maxSessionsPerAddress;
    hostConfig.session.timeoutMilliseconds = options.idleTimeoutSeconds * 1000U;
    s.host = std::make_unique<Host>(*s.transport, hostConfig, *s.keys);
    s.host->Diagnostics().SetSink([&s](std::string_view message) { s.LogMessage(std::string(message)); });
    for (const DiagEntry& entry : s.host->Diagnostics().Entries()) {
        s.LogMessage(std::string(DiagName(entry.code)) + ": " + entry.detail);
    }
    if (!s.host->Valid()) {
        error = "the network host could not be created (random numbers unavailable?)";
        return false;
    }
    ReplicationConfig replicationConfig;
    replicationConfig.visibilityRadius = options.visibilityRadius;
    replicationConfig.gridPlane = options.gridPlane;
    replicationConfig.tickRate = options.tickRate;
#if defined(_WIN32)
    char profileValue[2]{};
    std::size_t profileLength = 0U;
    replicationConfig.profile = getenv_s(&profileLength, profileValue, sizeof(profileValue), "KB_NET_PROFILE") == 0 && profileLength > 1U;
#else
    replicationConfig.profile = std::getenv("KB_NET_PROFILE") != nullptr;
#endif
    replicationConfig.minClientRate = options.minClientRate;
    replicationConfig.maxClientRate = options.maxClientRate;
    s.profileMetrics = replicationConfig.profile;
    s.replication = std::make_unique<ReplicationServer>(*s.host, s.scene->Runtime().EcsWorld(), *s.components, *s.rpcs, replicationConfig);
    s.net->BindServer(*s.replication);
    if (options.replicate != ReplicateMode::None) {
        s.net->SetTransformAppeared([&s](kb::ecs::Entity entity) { s.candidates.push_back(entity); });
        s.scene->Runtime().EcsWorld().ForEach<kb::scene::TransformComponent>(
            [](kb::ecs::Entity entity, const kb::scene::TransformComponent&, void* context) { static_cast<Impl*>(context)->candidates.push_back(entity); }, &s);
    }
    s.replication->SetRpcHandler("Ping", [&s](std::uint64_t caller, std::span<const RpcValue> args) {
        const std::vector<RpcValue> reply{ std::get<std::uint32_t>(args[0]) };
        static_cast<void>(s.replication->CallRpc(caller, "Pong", reply));
    });
    s.replication->SetRpcHandler("Say", [&s](std::uint64_t caller, std::span<const RpcValue> args) {
        s.LogMessage("client " + std::to_string(caller) + " says: " + std::get<std::string>(args[0]));
        const std::vector<RpcValue> message{ static_cast<std::uint32_t>(caller), std::get<std::string>(args[0]) };
        RpcStatus status = RpcStatus::Ok;
        s.replication->CallRpcAll("Said", message, status);
    });

    if (!options.authProviderKeyFile.empty()) {
        AuthGateConfig config;
        config.maxCommandBytes = hostConfig.session.maxMessageBytes - 1U;
        std::ifstream keyFile(options.authProviderKeyFile);
        std::string hex, trailing;
        if (!(keyFile >> hex) || (keyFile >> trailing) || !kb::security::TryParseHex(hex, config.providerKey)) {
            error = "the authorization provider key must contain exactly 64 hex digits";
            return false;
        }
        AuthGateHooks hooks;
        hooks.onAuthenticated = [&s](std::uint64_t peer, const AuthClaims& claims) {
            if ((claims.scopes & kScopePlay) == 0U) {
                s.RemoveAuthorizedPeer(peer, DisconnectReason::Requested);
                static_cast<void>(s.auth->Authorize(peer, kScopePlay));
            } else if (s.authorizedPeers.insert(peer).second) {
                s.HandleHostEvent(NetEvent{ .type = NetEventType::Connected, .peer = peer }, s.lastNow);
            }
        };
        hooks.onKicked = [&s](std::uint64_t peer, AuthKickReason) {
            s.RemoveAuthorizedPeer(peer, DisconnectReason::Requested);
        };
        hooks.onDisconnected = [&s](std::uint64_t peer, DisconnectReason reason) {
            s.RemoveAuthorizedPeer(peer, reason);
        };
        s.auth = std::make_unique<AuthGate>(*s.host, config, std::move(hooks));
        if (!s.ReloadRevocations()) {
            error = "the initial authorization revocation snapshot is invalid";
            return false;
        }
    }
    return true;
}

void SceneServer::Pump(std::uint64_t nowMilliseconds) {
    impl_->Pump(nowMilliseconds);
}

void SceneServer::Tick(std::uint32_t tick, std::uint64_t nowMilliseconds) {
    impl_->SyncReplicatedEntities();
    impl_->replication->Tick(tick, nowMilliseconds);
}

} // namespace kb::net

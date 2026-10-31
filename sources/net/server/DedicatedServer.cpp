#include "kb/net/WebSocketTransport.hpp"
#include "DedicatedServer.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace kb::net::server {

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] bool ParseAddress(const std::string& text, std::uint32_t& address) {
    std::uint32_t parts[4]{};
    std::size_t index = 0U;
    std::size_t position = 0U;
    while (index < 4U) {
        std::size_t consumed = 0U;
        unsigned long value = 0UL;
        try {
            value = std::stoul(text.substr(position), &consumed);
        } catch (...) {
            return false;
        }
        if (consumed == 0U || value > 255UL) {
            return false;
        }
        parts[index++] = static_cast<std::uint32_t>(value);
        position += consumed;
        if (index < 4U) {
            if (position >= text.size() || text[position] != '.') {
                return false;
            }
            ++position;
        }
    }
    if (position != text.size()) {
        return false;
    }
    address = (parts[0] << 24U) | (parts[1] << 16U) | (parts[2] << 8U) | parts[3];
    return true;
}

void WriteFileAtomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << text;
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        std::filesystem::rename(temporary, path, error);
    }
}

} // namespace

bool ParseKeysFile(const std::string& text, StaticKeyTable& keys, std::size_t& count, std::string& error) {
    std::istringstream stream(text);
    std::string line;
    std::size_t number = 0U;
    count = 0U;
    while (std::getline(stream, line)) {
        ++number;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line.erase(hash);
        }
        std::istringstream fields(line);
        std::string id;
        std::string hex;
        std::string extra;
        if (!(fields >> id)) {
            continue; // blank
        }
        if (!(fields >> hex) || (fields >> extra)) {
            error = "keys file line " + std::to_string(number) + ": expected '<client id> <64 hex digits>'";
            return false;
        }
        std::uint64_t clientId = 0U;
        try {
            std::size_t consumed = 0U;
            clientId = std::stoull(id, &consumed);
            if (consumed != id.size()) {
                throw std::invalid_argument("id");
            }
        } catch (...) {
            error = "keys file line " + std::to_string(number) + ": the client id is not a number";
            return false;
        }
        SessionKey key{};
        if (clientId == 0U || !kb::security::TryParseHex(hex, key)) {
            error = "keys file line " + std::to_string(number) + ": the client id must be above zero and the key 64 hex digits";
            return false;
        }
        keys.Set(clientId, key);
        ++count;
    }
    return true;
}

std::string SummaryJson(const ServerOptions& options, const ServerSummary& summary) {
    using kb::core::JsonValue;
    const auto number = [](std::uint64_t value) { return JsonValue::MakeNumber(static_cast<double>(value)); };
    auto root = JsonValue::MakeObject();
    root.Set("schema", JsonValue::MakeString("21kb.dedicated-server/v1"));
    root.Set("exit_reason", JsonValue::MakeString(summary.exitReason));
    root.Set("project", JsonValue::MakeString(options.project.generic_string()));
    root.Set("port", number(summary.port));
    root.Set("websocket_port", number(summary.webSocketPort));
    root.Set("state_loaded", JsonValue::MakeBool(summary.stateLoaded));
    root.Set("state_saved", JsonValue::MakeBool(summary.stateSaved));
    root.Set("tick_rate", number(options.tickRate));
    root.Set("ticks", number(summary.ticks));
    root.Set("uptime_ms", number(summary.uptimeMilliseconds));
    root.Set("max_clients", number(options.maxClients));
    root.Set("clients_now", number(summary.clientsNow));
    root.Set("clients_peak", number(summary.clientsPeak));
    root.Set("replicated_entities", number(summary.replicatedEntities));
    auto authorization = JsonValue::MakeObject();
    authorization.Set("enabled", JsonValue::MakeBool(!options.authProviderKeyFile.empty()));
    authorization.Set("clients", number(summary.authenticatedClients));
    authorization.Set("accepted", number(summary.authorization.loginsAccepted));
    authorization.Set("refused", number(summary.authorization.loginsRefused));
    authorization.Set("commands_refused", number(summary.authorization.commandsRefused));
    authorization.Set("kicked", number(summary.authorization.kicked));
    root.Set("authorization", std::move(authorization));
    auto host = JsonValue::MakeObject();
    host.Set("datagrams_received", number(summary.host.datagramsReceived));
    host.Set("oversized", number(summary.host.oversized));
    host.Set("malformed", number(summary.host.malformed));
    host.Set("challenges_sent", number(summary.host.challengesSent));
    host.Set("handshakes_completed", number(summary.host.handshakesCompleted));
    host.Set("handshakes_rejected", number(summary.host.handshakesRejected));
    host.Set("unknown_peer", number(summary.host.unknownPeer));
    host.Set("rate_limited", number(summary.host.rateLimited));
    root.Set("host", std::move(host));
    auto replication = JsonValue::MakeObject();
    const ReplicationServerStats& r = summary.replication;
    replication.Set("spawns_sent", number(r.spawnsSent));
    replication.Set("despawns_sent", number(r.despawnsSent));
    replication.Set("state_messages_sent", number(r.stateMessagesSent));
    replication.Set("entries_sent", number(r.entriesSent));
    replication.Set("acks_received", number(r.acksReceived));
    replication.Set("send_busy", number(r.sendBusy));
    replication.Set("rejected_malformed", number(r.rejectedMalformed));
    replication.Set("hello_mismatch", number(r.helloMismatch));
    replication.Set("rpc_received", number(r.rpcReceived));
    replication.Set("rpc_dispatched", number(r.rpcDispatched));
    replication.Set("rpc_rejected_permission", number(r.rpcRejectedPermission));
    replication.Set("rpc_rejected_rate", number(r.rpcRejectedRate));
    replication.Set("rpc_rejected_arguments", number(r.rpcRejectedArguments + r.rpcRejectedMalformed + r.rpcRejectedDirection));
    root.Set("replication", std::move(replication));
    return root.Dump();
}

struct DedicatedServer::Impl {
    Impl(ServerOptions serverOptions, std::ostream& logStream) : options(std::move(serverOptions)), log(&logStream) {}

    void Log(const std::string& message) const {
        if (options.quiet) {
            return;
        }
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
        *log << '[' << std::setw(7) << milliseconds << " ms] " << message << '\n';
        log->flush();
    }

    [[nodiscard]] std::uint64_t NowMilliseconds() const {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count());
    }

    void RefreshSummary() {
        summary.ticks = tick;
        summary.uptimeMilliseconds = NowMilliseconds();
        summary.clientsNow = host != nullptr ? host->ClientCount() : 0U;
        summary.clientsPeak = std::max(summary.clientsPeak, summary.clientsNow);
        summary.replicatedEntities = replication != nullptr ? replication->ReplicatedCount() : 0U;
        if (host != nullptr) {
            summary.host = host->Stats();
        }
        if (replication != nullptr) {
            summary.replication = replication->Stats();
        }
        if (auth != nullptr) {
            summary.authenticatedClients = authorizedPeers.size();
            summary.authorization = auth->GetStats();
        }
    }

    void SyncReplicatedEntities() {
        if (options.replicate == ReplicateMode::None) {
            return;
        }
        struct Collector {
            std::vector<kb::ecs::Entity>* found;
        } collector{ &candidates };
        candidates.clear();
        scene->Runtime().EcsWorld().ForEach<kb::scene::TransformComponent>(
            [](kb::ecs::Entity entity, const kb::scene::TransformComponent&, void* context) {
                static_cast<Collector*>(context)->found->push_back(entity);
            },
            &collector);
        for (const kb::ecs::Entity entity : candidates) {
            if (!considered.insert(entity.Id()).second) {
                continue;
            }
            if (options.replicate == ReplicateMode::Prefix) {
                const std::string name{ scene->Entities().Name(entity) };
                if (name.rfind(options.replicatePrefix, 0U) != 0U) {
                    continue;
                }
            }
            if (replication->Replicate(entity, 0U) == 0U) {
                Log("warning: an entity could not be replicated (limit reached)");
            }
        }
    }

    void HandleHostEvent(const NetEvent& event, std::uint64_t now) {
        switch (event.type) {
        case NetEventType::Connected:
            Log("client " + std::to_string(event.peer) + " connected");
            break;
        case NetEventType::Disconnected:
            Log("client " + std::to_string(event.peer) + " disconnected");
            break;
        case NetEventType::Message:
            break;
        }
        net->HandleEvent(event);
        replication->HandleEvent(event, now);
    }

    void RemoveAuthorizedPeer(std::uint64_t peer, DisconnectReason reason) {
        if (authorizedPeers.erase(peer) != 0U) {
            HandleHostEvent(NetEvent{ .type = NetEventType::Disconnected, .peer = peer, .reason = reason }, NowMilliseconds());
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
        if (!valid && !revocationErrorReported) Log("authorization: revocation snapshot rejected; retaining the last valid snapshot");
        revocationErrorReported = !valid;
        return valid;
    }

    void PumpHost(std::uint64_t now) {
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

    void WriteStatus() {
        if (options.statusFile.empty()) {
            return;
        }
        RefreshSummary();
        WriteFileAtomically(options.statusFile, SummaryJson(options, summary));
    }

    ServerOptions options;
    std::ostream* log;
    Clock::time_point started = Clock::now();
    ServerSummary summary;
    StaticKeyTable keys;
    std::unique_ptr<kb::scene::Scene> scene;
    std::unique_ptr<kb::script::ScriptRuntimeHost> scripts;
    std::unique_ptr<ScriptNetworkRuntime> net;
    std::unique_ptr<ReplicationRegistry> components;
    std::unique_ptr<RpcRegistry> rpcs;
    std::unique_ptr<IDatagramTransport> transport;
    std::unique_ptr<Host> host;
    std::unique_ptr<AuthGate> auth;
    std::unordered_set<std::uint64_t> authorizedPeers;
    std::uint64_t nextRevocationReload = 0U;
    bool revocationErrorReported = false;
    std::unique_ptr<ReplicationServer> replication;
    std::vector<kb::ecs::Entity> candidates;
    std::unordered_set<kb::ecs::Entity::IdType> considered;
    std::uint32_t tick = 0U;
};

DedicatedServer::DedicatedServer(ServerOptions options, std::ostream& log) : impl_(std::make_unique<Impl>(std::move(options), log)) {}

DedicatedServer::~DedicatedServer() = default;

const ServerSummary& DedicatedServer::Summary() const noexcept {
    return impl_->summary;
}

bool DedicatedServer::Start(std::string& error) {
    Impl& s = *impl_;
    const ServerOptions& options = s.options;

    if (!options.stateFile.empty() && options.stateFile.extension() != ".21kbscene") {
        error = "--state-file must name a .21kbscene checkpoint";
        return false;
    }
    if (!options.authRevocationsFile.empty() && options.authProviderKeyFile.empty()) {
        error = "--auth-revocations requires --auth-provider-key";
        return false;
    }

    std::uint32_t address = 0U;
    if (!ParseAddress(options.bindAddress, address)) {
        error = "the bind address is not an IPv4 address: " + options.bindAddress;
        return false;
    }
    if (options.tickRate < 10U || options.tickRate > 240U) {
        error = "the tick rate must be between 10 and 240";
        return false;
    }
    if (options.maxClients == 0U || options.maxClients > 4096U) {
        error = "the client limit must be between 1 and 4096";
        return false;
    }
    if (options.idleTimeoutSeconds < 2U || options.idleTimeoutSeconds > 3600U) {
        error = "the idle timeout must be between 2 and 3600 seconds";
        return false;
    }

    std::size_t keyCount = 0U;
    {
        std::ifstream input(options.keysFile, std::ios::binary);
        if (!input) {
            error = "the keys file could not be opened: " + options.keysFile.generic_string();
            return false;
        }
        std::ostringstream text;
        text << input.rdbuf();
        if (!ParseKeysFile(text.str(), s.keys, keyCount, error)) {
            return false;
        }
        if (keyCount == 0U) {
            error = "the keys file has no keys: nobody could connect";
            return false;
        }
    }

    // The project and the scene, the way kb_cli run loads them: no window, no renderer.
    std::error_code pathError;
    const std::filesystem::path projectRoot = std::filesystem::absolute(options.project, pathError).lexically_normal();
    if (pathError || !std::filesystem::is_directory(projectRoot, pathError)) {
        error = "the project directory was not found: " + options.project.generic_string();
        return false;
    }
    std::string projectError;
    const std::filesystem::path projectFile = kb::project::ProjectManager::FindProjectFile(projectRoot, projectError);
    if (projectFile.empty()) {
        error = projectError;
        return false;
    }
    kb::project::ProjectDescriptorReadResult loaded = kb::project::ProjectManager::LoadProject(projectFile);
    if (!loaded.succeeded) {
        error = "the project descriptor could not be loaded: " + loaded.error;
        return false;
    }
    for (kb::project::ProjectPluginReference& plugin : loaded.descriptor.plugins) {
        const std::filesystem::path configured{ plugin.binaryPath };
        if (!plugin.enabled || configured.empty() || configured.is_absolute()) {
            continue;
        }
        std::error_code localError;
        if (std::filesystem::is_regular_file(projectRoot / configured, localError) && !localError) {
            plugin.binaryPath = (projectRoot / configured).string();
        }
    }
    s.scene = std::make_unique<kb::scene::Scene>(std::move(loaded.descriptor));
    if (!s.scene->ModuleDiagnostics().empty()) {
        error = "a project module did not load: " + s.scene->ModuleDiagnostics().front();
        return false;
    }
    if (!s.scene->Assets().MountProject(projectRoot)) {
        error = "the project assets could not be mounted under " + projectRoot.generic_string();
        return false;
    }
    static_cast<void>(s.scene->Assets().Discover());

    std::string sceneOption = options.scene;
    if (sceneOption.empty()) {
        sceneOption = kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(projectRoot)).settings.defaultMap;
    }
    if (sceneOption.empty()) {
        error = "the project has no startup scene; pass --scene <path>";
        return false;
    }
    std::filesystem::path scenePath;
    if (s.scene->Assets().Manager().Mounts().Resolve(sceneOption).has_value()) {
        const kb::assets::AssetMetadata* metadata = s.scene->Assets().Manager().Registry().FindByPath(sceneOption);
        if (metadata == nullptr) {
            error = "the scene asset was not found: " + sceneOption;
            return false;
        }
        scenePath = metadata->physicalPath;
    } else {
        scenePath = std::filesystem::path{ sceneOption };
        if (scenePath.is_relative()) {
            scenePath = projectRoot / scenePath;
        }
    }
    if (!options.stateFile.empty()) {
        std::error_code stateError;
        const bool exists = std::filesystem::exists(options.stateFile, stateError);
        if (stateError) {
            error = "the server checkpoint could not be inspected: " + stateError.message();
            return false;
        }
        if (exists) {
            scenePath = options.stateFile;
            s.summary.stateLoaded = true;
        }
    }
    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(*s.scene, scenePath)) {
        error = "the scene could not be loaded: " + scenePath.generic_string();
        return false;
    }
    s.scripts = std::make_unique<kb::script::ScriptRuntimeHost>(*s.scene);
    if (!s.scripts->Succeeded()) {
        error = "the script host failed: " + (s.scripts->Diagnostics().empty() ? std::string{ "unknown error" } : s.scripts->Diagnostics().front());
        return false;
    }
    // Net.* is attached before the scripts start, so that their Created and Ready can declare RPCs and mark entities.
    s.net = std::make_unique<ScriptNetworkRuntime>(ScriptNetworkRuntime::Role::Server, *s.scene);
    if (!s.scripts->InstallSceneSystem()) {
        error = "the script scene system could not be installed";
        return false;
    }
    for (const std::string& problem : s.scene->Runtime().DrainSceneSystemErrors()) {
        s.Log("[scene system error] " + problem);
    }

    // The network.
    s.components = MakeSceneReplicationRegistry(options.worldExtent);
    if (s.components == nullptr) {
        error = "the world extent is not valid";
        return false;
    }
    s.rpcs = std::make_unique<RpcRegistry>();
    DefineStandardRpcs(*s.rpcs);
    if (!s.net->Freeze(*s.rpcs, error)) {
        return false;
    }
    std::string bindError;
    if (options.webSocketEnabled) {
        WebSocketServerOptions web;
        web.udpBind = Endpoint{address, options.port};
        web.webSocketPort = options.webSocketPort;
        web.maxClients = options.maxClients;
        web.allowedOrigin = options.webSocketOrigin;
        s.transport = CreateWebSocketServerTransport(web, s.summary.webSocketPort, bindError);
    } else {
        s.transport = CreateUdpTransport(Endpoint{address, options.port}, bindError);
    }
    if (s.transport == nullptr) {
        error = std::string(options.webSocketEnabled ? "the network listener could not be opened: " :
            "the UDP socket could not be opened: ") + bindError;
        return false;
    }
    s.summary.port = s.transport->LocalEndpoint().port;
    HostConfig hostConfig;
    hostConfig.maxClients = options.maxClients;
    hostConfig.session.timeoutMilliseconds = options.idleTimeoutSeconds * 1000U;
    s.host = std::make_unique<Host>(*s.transport, hostConfig, s.keys);
    if (!s.host->Valid()) {
        error = "the network host could not be created (random numbers unavailable?)";
        return false;
    }
    s.replication = std::make_unique<ReplicationServer>(*s.host, s.scene->Runtime().EcsWorld(), *s.components, *s.rpcs);
    s.net->BindServer(*s.replication);
    s.replication->SetRpcHandler("Ping", [&s](std::uint64_t caller, std::span<const RpcValue> args) {
        const std::vector<RpcValue> reply{ std::get<std::uint32_t>(args[0]) };
        static_cast<void>(s.replication->CallRpc(caller, "Pong", reply));
    });
    s.replication->SetRpcHandler("Say", [&s](std::uint64_t caller, std::span<const RpcValue> args) {
        s.Log("client " + std::to_string(caller) + " says: " + std::get<std::string>(args[0]));
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
                s.HandleHostEvent(NetEvent{ .type = NetEventType::Connected, .peer = peer }, s.NowMilliseconds());
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

    if (!options.portFile.empty()) {
        WriteFileAtomically(options.portFile, std::to_string(s.summary.port));
    }
    s.Log("loaded " + scenePath.filename().generic_string() + " from " + projectRoot.generic_string() + ", " + std::to_string(keyCount) + " client keys");
    s.Log("listening on " + options.bindAddress + ":" + std::to_string(s.summary.port) + ", " + std::to_string(options.tickRate) + " ticks per second, up to " +
        std::to_string(options.maxClients) + " clients");
    return true;
}

int DedicatedServer::Run(const std::atomic<bool>& stop) {
    Impl& s = *impl_;
    const ServerOptions& options = s.options;
    const std::uint64_t tickMicroseconds = 1'000'000ULL / options.tickRate;
    const float stepSeconds = 1.0F / static_cast<float>(options.tickRate);
    std::uint64_t nextTickAt = 0U; // microseconds since the start
    std::uint64_t nextStatusAt = 0U;
    std::uint64_t emptySince = 0U;
    bool hadClients = false;
    const char* reason = "error";
    int exitCode = 0;

    for (;;) {
        const std::uint64_t now = s.NowMilliseconds();
        s.PumpHost(now);

        std::size_t steps = 0U;
        while (nextTickAt <= now * 1000ULL && steps < 5U) {
            static_cast<void>(s.scene->Runtime().Update(stepSeconds));
            for (const std::string& problem : s.scene->Runtime().DrainSceneSystemErrors()) {
                s.Log("[scene system error] " + problem);
            }
            s.SyncReplicatedEntities();
            s.replication->Tick(++s.tick, now);
            nextTickAt += tickMicroseconds;
            ++steps;
        }
        if (steps == 5U && nextTickAt <= now * 1000ULL) {
            nextTickAt = now * 1000ULL + tickMicroseconds; // too far behind: drop the backlog instead of spiralling
        }

        if (now >= nextStatusAt) {
            s.WriteStatus();
            nextStatusAt = now + 1000U;
        }
        const std::size_t clients = s.host->ClientCount();
        s.summary.clientsPeak = std::max(s.summary.clientsPeak, clients);
        if (clients != 0U) {
            hadClients = true;
            emptySince = 0U;
        } else if (hadClients && emptySince == 0U) {
            emptySince = now;
        }

        if (stop.load()) {
            reason = "signal";
            break;
        }
        if (!options.stopFile.empty()) {
            std::error_code fileError;
            if (std::filesystem::exists(options.stopFile, fileError)) {
                reason = "stop-file";
                break;
            }
        }
        if (options.maxSeconds != 0U && now >= static_cast<std::uint64_t>(options.maxSeconds) * 1000ULL) {
            reason = "max-seconds";
            break;
        }
        if (options.emptyTimeoutSeconds != 0U && emptySince != 0U && now - emptySince >= static_cast<std::uint64_t>(options.emptyTimeoutSeconds) * 1000ULL) {
            reason = "empty-timeout";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    s.Log(std::string("stopping (") + reason + "): telling " + std::to_string(s.host->ClientCount()) + " clients");
    for (const std::uint64_t client : s.host->ClientIds()) {
        s.host->Disconnect(client);
    }
    // A few rounds so that the goodbyes are sent.
    const std::uint64_t shutdownStarted = s.NowMilliseconds();
    while (s.NowMilliseconds() - shutdownStarted < 300U && s.host->ClientCount() != 0U) {
        s.PumpHost(s.NowMilliseconds());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (exitCode == 0 && !options.stateFile.empty()) {
        s.summary.stateSaved = kb::scene::SceneDocumentService::Save(*s.scene, options.stateFile, "ServerState");
        if (!s.summary.stateSaved) {
            s.Log("error: the server checkpoint could not be saved");
            reason = "state-save-error";
            exitCode = 1;
        }
    }
    s.scripts.reset();
    s.summary.exitReason = reason;
    s.RefreshSummary();
    s.WriteStatus();
    s.Log("stopped after " + std::to_string(s.summary.ticks) + " ticks");
    return exitCode;
}

} // namespace kb::net::server

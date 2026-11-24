#include "kb/net/WebRtcTransport.hpp"
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
#include "engine/security/ReleaseManifest.hpp"
#include "engine/security/ReleaseKeys.hpp"
#include "kb/net/ProjectScene.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <cstdlib>
#include <iomanip>
#include <limits>
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
        if (clientId <= kHostPlayerId || !kb::security::TryParseHex(hex, key)) {
            error = "keys file line " + std::to_string(number) + ": client ids 0 and 1 are reserved; expected 64 hex key digits";
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
    host.Set("connect_buckets_full", number(summary.host.connectBucketsFull));
    host.Set("password_attempts", number(summary.host.passwordAttempts));
    host.Set("password_rejected", number(summary.host.passwordRejected));
    host.Set("password_rate_limited", number(summary.host.passwordRateLimited));
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
    auto diagnostics = JsonValue::MakeArray();
    for (const DiagEntry& entry : summary.diagnostics) {
        auto item = JsonValue::MakeObject();
        item.Set("code", JsonValue::MakeString(std::string(DiagName(entry.code))));
        item.Set("count", number(entry.count));
        item.Set("last_ms", number(entry.lastMs));
        item.Set("detail", JsonValue::MakeString(entry.detail));
        diagnostics.Append(std::move(item));
    }
    root.Set("diagnostics", std::move(diagnostics));
    root.Set("diag_total", number(summary.diagTotal));
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
        Host* host = network != nullptr ? &network->NetHost() : nullptr;
        ReplicationServer* replication = network != nullptr ? &network->Replication() : nullptr;
        summary.clientsNow = host != nullptr ? host->ClientCount() : 0U;
        summary.clientsPeak = std::max(summary.clientsPeak, summary.clientsNow);
        summary.replicatedEntities = replication != nullptr ? replication->ReplicatedCount() : 0U;
        if (host != nullptr) {
            summary.host = host->Stats();
            const auto entries = host->Diagnostics().Entries();
            summary.diagnostics.assign(entries.begin(), entries.end());
            summary.diagTotal = host->Diagnostics().Total();
        }
        if (replication != nullptr) {
            summary.replication = replication->Stats();
        }
        if (AuthGate* auth = network != nullptr ? network->Authorization() : nullptr) {
            summary.authenticatedClients = network->AuthorizedPeers();
            summary.authorization = auth->GetStats();
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
    std::unique_ptr<SceneServer> network;
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
    if (!std::isfinite(options.visibilityRadius) || options.visibilityRadius < 0.0F) {
        error = "visibility radius must be finite and nonnegative";
        return false;
    }
    if (options.minClientRate == 0U || options.minClientRate > options.maxClientRate) { error = "invalid client byte rate limits"; return false; }
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
    OpenedProjectScene opened;
    if (!OpenProjectScene(options.project, options.scene, opened, error)) {
        return false;
    }
    const std::filesystem::path& projectRoot = opened.projectRoot;
    std::filesystem::path scenePath = opened.scenePath;
    s.scene = std::move(opened.scene);
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
    s.net = std::make_unique<ScriptNetworkRuntime>(ScriptNetworkRuntime::Role::Server, *s.scene, opened.replication);
    if (!s.scripts->InstallSceneSystem()) {
        error = "the script scene system could not be installed";
        return false;
    }
    for (const std::string& problem : s.scene->Runtime().DrainSceneSystemErrors()) {
        s.Log("[scene system error] " + problem);
    }

    // The listener is opened here: the dedicated server decides between UDP, WebSocket and WebRTC and hands the socket over.
    std::string bindError;
    std::unique_ptr<IDatagramTransport> transport;
    if (options.webSocketEnabled) {
        WebSocketServerOptions web;
        web.udpBind = Endpoint{address, options.port};
        web.webSocketPort = options.webSocketPort;
        web.maxClients = options.maxClients;
        web.allowedOrigin = options.webSocketOrigin;
        std::shared_ptr<SignalingListener> signaling;
        if (options.webRtcEnabled) {
#if defined(KB_NET_WEBRTC)
            signaling = web.signaling = std::make_shared<SignalingListener>();
#else
            error = "WebRTC support was not enabled in this build";
            return false;
#endif
        }
        transport = CreateWebSocketServerTransport(web, s.summary.webSocketPort, bindError);
#if defined(KB_NET_WEBRTC)
        if (transport != nullptr && signaling != nullptr) {
            WebRtcServerOptions rtc;
            rtc.link.iceServers = options.iceServers;
            rtc.maxClients = options.maxClients;
            transport = CreateWebRtcServerTransport(rtc, std::move(signaling), std::move(transport), bindError);
        }
#endif
    } else {
        transport = CreateUdpTransport(Endpoint{address, options.port}, bindError);
    }
    if (transport == nullptr) {
        error = std::string(options.webSocketEnabled ? "the network listener could not be opened: " :
            "the UDP socket could not be opened: ") + bindError;
        return false;
    }
    s.summary.port = transport->LocalEndpoint().port;

    std::uint32_t release = 0U;
    std::string contentVersion;
    const auto manifestPath = projectRoot / std::filesystem::path(kb::security::kReleaseManifestFileName);
    std::error_code manifestError;
    const bool hasManifest = std::filesystem::exists(manifestPath, manifestError);
    if (manifestError) { error = "release manifest could not be inspected"; return false; }
    if (hasManifest) {
        const auto anchor = kb::security::FindReleaseTrustAnchor(projectRoot);
        if (anchor.state != kb::security::TrustAnchorLookup::State::Present) {
            error = "release manifest requires a valid release trust anchor";
            return false;
        }
        std::ifstream input(manifestPath, std::ios::binary);
        std::ostringstream text;
        text << input.rdbuf();
        kb::security::ReleaseManifest manifest;
        if (!input || kb::security::ParseAndVerifyReleaseManifest(text.str(), anchor.anchor.releaseKey, manifest) !=
            kb::security::ReleaseManifestStatus::Success || manifest.releaseNumber > std::numeric_limits<std::uint32_t>::max()) {
            error = "release manifest is invalid or its release number exceeds the protocol limit";
            return false;
        }
        release = static_cast<std::uint32_t>(manifest.releaseNumber);
        contentVersion = manifest.contentVersion;
    }
    if (options.release) release = *options.release;
    if (options.contentVersion) contentVersion = *options.contentVersion;

    SceneServerOptions networkOptions;
    networkOptions.maxClients = options.maxClients;
    networkOptions.maxConnectsPerSecondPerAddress = options.maxConnectsPerSecondPerAddress;
    networkOptions.maxSessionsPerAddress = options.maxSessionsPerAddress;
    networkOptions.tickRate = options.tickRate;
    networkOptions.minClientRate = options.minClientRate;
    networkOptions.maxClientRate = options.maxClientRate;
    networkOptions.idleTimeoutSeconds = options.idleTimeoutSeconds;
    networkOptions.replicate = options.replicate;
    networkOptions.replicatePrefix = options.replicatePrefix;
    networkOptions.worldExtent = options.worldExtent;
    networkOptions.visibilityRadius = options.visibilityRadius;
    networkOptions.gridPlane = options.gridPlane;
    networkOptions.identity = MakeBuildIdentity(release, contentVersion);
    networkOptions.authProviderKeyFile = options.authProviderKeyFile;
    networkOptions.authRevocationsFile = options.authRevocationsFile;
    s.network = std::make_unique<SceneServer>(*s.scene, *s.net, opened.replication, s.keys, std::move(transport),
        std::move(networkOptions), [&s](const std::string& message) { s.Log(message); });
    if (!s.network->Start(error)) {
        return false;
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

    std::ofstream metrics;
    if (!options.metricsFile.empty()) {
        metrics.open(options.metricsFile);
        if (!metrics) { s.Log("error: metrics file could not be opened"); return 1; }
        metrics << "second,tick_avg_ms,tick_max_ms,pump_ms,replication_ms,clients,state_msgs,bytes_out,bytes_in,datagrams_in,diag_total";
        if (s.network->ProfileMetrics()) metrics << ",visibility_ms,candidates_ms,ordering_ms,encoding_ms";
        metrics << '\n';
    }
    double tickSum = 0.0, tickMax = 0.0, pumpSum = 0.0, replicationSum = 0.0;
    std::uint64_t measuredTicks = 0U;
    const auto elapsedMs = [](Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    for (;;) {
        const std::uint64_t now = s.NowMilliseconds();
        const auto pumpStart = Clock::now();
        s.network->Pump(now);
        pumpSum += elapsedMs(pumpStart);

        std::size_t steps = 0U;
        while (nextTickAt <= now * 1000ULL && steps < 5U) {
            const auto tickStart = Clock::now();
            static_cast<void>(s.scene->Runtime().Update(stepSeconds));
            for (const std::string& problem : s.scene->Runtime().DrainSceneSystemErrors()) {
                s.Log("[scene system error] " + problem);
            }
            const auto replicationStart = Clock::now();
            s.network->Tick(++s.tick, now);
            replicationSum += elapsedMs(replicationStart);
            const auto tickMs = elapsedMs(tickStart);
            tickSum += tickMs;
            tickMax = std::max(tickMax, tickMs);
            ++measuredTicks;
            nextTickAt += tickMicroseconds;
            ++steps;
        }
        if (steps == 5U && nextTickAt <= now * 1000ULL) {
            nextTickAt = now * 1000ULL + tickMicroseconds; // too far behind: drop the backlog instead of spiralling
        }

        if (now >= nextStatusAt) {
            s.WriteStatus();
            if (metrics.is_open()) {
                Host& host = s.network->NetHost();
                ReplicationServer& replication = s.network->Replication();
                std::uint64_t bytesOut = 0U, bytesIn = 0U;
                for (const auto id : host.ClientIds()) {
                    if (const auto* stats = host.ClientStats(id)) {
                        bytesOut += stats->bytesSent;
                        bytesIn += stats->bytesAccepted;
                    }
                }
                metrics << now / 1000U << ',' << (measuredTicks == 0U ? 0.0 : tickSum / static_cast<double>(measuredTicks))
                    << ',' << tickMax << ',' << pumpSum << ',' << replicationSum << ',' << host.ClientCount()
                    << ',' << replication.Stats().stateMessagesSent << ',' << bytesOut << ',' << bytesIn
                    << ',' << host.Stats().datagramsReceived << ',' << host.Diagnostics().Total();
                if (s.network->ProfileMetrics()) metrics << ',' << replication.Stats().visibilityNs / 1e6 << ',' << replication.Stats().candidatesNs / 1e6
                    << ',' << replication.Stats().orderingNs / 1e6 << ',' << replication.Stats().encodingNs / 1e6;
                metrics << '\n';
                tickSum = tickMax = pumpSum = replicationSum = 0.0;
                measuredTicks = 0U;
                metrics.flush();
            }
            nextStatusAt = now + 1000U;
        }
        const std::size_t clients = s.network->NetHost().ClientCount();
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
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - s.started).count();
        const auto remaining = nextTickAt > static_cast<std::uint64_t>(elapsed) ? nextTickAt - static_cast<std::uint64_t>(elapsed) : 0U;
        static_cast<void>(s.network->Transport().WaitReadable(static_cast<std::uint32_t>(std::min<std::uint64_t>(1U, remaining / 1000U))));
    }

    s.Log(std::string("stopping (") + reason + "): telling " + std::to_string(s.network->NetHost().ClientCount()) + " clients");
    for (const std::uint64_t client : s.network->NetHost().ClientIds()) {
        s.network->NetHost().Disconnect(client);
    }
    // A few rounds so that the goodbyes are sent.
    const std::uint64_t shutdownStarted = s.NowMilliseconds();
    while (s.NowMilliseconds() - shutdownStarted < 300U && s.network->NetHost().ClientCount() != 0U) {
        s.network->Pump(s.NowMilliseconds());
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

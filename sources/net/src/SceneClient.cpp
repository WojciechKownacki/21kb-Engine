#include "kb/net/SceneClient.hpp"
#include "kb/net/ProjectScene.hpp"
#include "engine/security/ReleaseManifest.hpp"
#include "kb/net/Auth.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/Prediction.hpp"
#include "kb/net/NetworkClock.hpp"
#include "kb/net/ScriptBridge.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/core/JsonValue.hpp"

#include <algorithm>
#include <limits>
#include <charconv>
#include <cmath>
#include <map>
#include <optional>
#include <ostream>

namespace kb::net {
namespace {
kb::scene::TransformComponent LerpTransform(const kb::scene::TransformComponent& from, const kb::scene::TransformComponent& to, double fraction) {
    const float t = static_cast<float>(fraction);
    kb::scene::TransformComponent result = to;
    result.localPosition = { from.localPosition.x + (to.localPosition.x - from.localPosition.x) * t, from.localPosition.y + (to.localPosition.y - from.localPosition.y) * t,
        from.localPosition.z + (to.localPosition.z - from.localPosition.z) * t };
    result.localRotation = kb::math::Slerp(from.localRotation, to.localRotation, t);
    result.localScale = { from.localScale.x + (to.localScale.x - from.localScale.x) * t, from.localScale.y + (to.localScale.y - from.localScale.y) * t,
        from.localScale.z + (to.localScale.z - from.localScale.z) * t };
    result.worldPosition = result.localPosition;
    result.worldRotation = result.localRotation;
    result.worldScale = result.localScale;
    return result;
}

bool ParseSessionIdentity(const kb::core::JsonValue& root, SceneClientOptions& parsed) {
    const auto* id = root.Find("clientId");
    const auto* key = root.Find("key");
    if (!id || !key ||
        id->GetKind() != kb::core::JsonValue::Kind::String || key->GetKind() != kb::core::JsonValue::Kind::String) return false;
    const auto& text = id->AsString();
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), parsed.clientId);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || parsed.clientId == 0U) return false;
    const auto& hex = key->AsString();
    if (hex.size() != 64U) return false;
    for (std::size_t i = 0; i < parsed.key.size(); ++i) {
        unsigned int byte = 0U;
        const auto result = std::from_chars(hex.data() + i * 2U, hex.data() + i * 2U + 2U, byte, 16);
        if (result.ec != std::errc{} || result.ptr != hex.data() + i * 2U + 2U) return false;
        parsed.key[i] = static_cast<std::uint8_t>(byte);
    }
    if (const auto* token = root.Find("token")) {
        if (token->GetKind() != kb::core::JsonValue::Kind::String || token->AsString().size() > 1024U) return false;
        parsed.token = token->AsString();
    }
    if (const auto* extent = root.Find("worldExtent")) {
        const double value = extent->AsNumber(-1.0);
        if (!std::isfinite(value) || value <= 0.0 || value > 1.0e7) return false;
        parsed.worldExtent = static_cast<float>(value);
    }
    if (const auto* attempts = root.Find("reconnectAttempts")) {
        const double value = attempts->AsNumber(-1.0);
        if (!std::isfinite(value) || value < 0.0 || value > 10.0 || std::floor(value) != value) return false;
        parsed.reconnectAttempts = static_cast<std::uint32_t>(value);
    }
    if (const auto* rate = root.Find("downlinkBytesPerSecond")) {
        const double value = rate->AsNumber(-1.0);
        if (!std::isfinite(value) || value < 1.0 || value > UINT32_MAX || value != std::floor(value)) return false;
        parsed.downlinkBytesPerSecond = static_cast<std::uint32_t>(value);
    }
    return true;
}
} // namespace

bool ParseWebSceneClientSettings(std::string_view json, SceneClientOptions& options, std::string& url) {
    if (json.size() > 8192U) return false;
    kb::core::JsonValue root;
    std::string error;
    if (!kb::core::JsonValue::Parse(json, root, error) || root.GetKind() != kb::core::JsonValue::Kind::Object) return false;
    const auto* endpoint = root.Find("url");
    if (!endpoint || endpoint->GetKind() != kb::core::JsonValue::Kind::String) return false;
    const auto& address = endpoint->AsString();
    if (address.size() > 2048U || (!address.starts_with("ws://") && !address.starts_with("wss://")) ||
        address.find_first_of("\r\n") != std::string::npos || address.find('\0') != std::string::npos) return false;
    SceneClientOptions parsed;
    if (!ParseSessionIdentity(root, parsed)) return false;
    if (const auto* transport = root.Find("transport")) {
        if (transport->GetKind() != kb::core::JsonValue::Kind::String ||
            (transport->AsString() != "websocket" && transport->AsString() != "webrtc")) return false;
        parsed.webRtc = transport->AsString() == "webrtc";
    }
    if (const auto* servers = root.Find("iceServers")) {
        if (!parsed.webRtc || servers->GetKind() != kb::core::JsonValue::Kind::Array || servers->Size() > 8U) return false;
        for (std::size_t index = 0U; index < servers->Size(); ++index) {
            const auto* server = servers->At(index);
            if (server->GetKind() != kb::core::JsonValue::Kind::String || server->AsString().empty() || server->AsString().size() > 512U ||
                server->AsString().find_first_of("\r\n") != std::string::npos || server->AsString().find('\0') != std::string::npos) return false;
            parsed.iceServers.push_back(server->AsString());
        }
    }
    parsed.server = Endpoint::Loopback(1U);
    options = std::move(parsed);
    url = address;
    return true;
}

bool ParseIpv4Literal(std::string_view text, std::uint32_t& address) {
    std::uint32_t ipv4 = 0U;
    std::size_t start = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        const std::size_t end = index == 3U ? text.size() : text.find('.', start);
        if (end == std::string_view::npos || end <= start) return false;
        unsigned int octet = 0U;
        const auto value = std::from_chars(text.data() + start, text.data() + end, octet);
        if (value.ec != std::errc{} || value.ptr != text.data() + end || octet > 255U) return false;
        ipv4 = (ipv4 << 8U) | octet;
        start = end + 1U;
    }
    if (ipv4 == 0U || ipv4 == UINT32_MAX) return false;
    address = ipv4;
    return true;
}

bool ParseNativeSceneClientSettings(std::string_view json, SceneClientOptions& options) {
    if (json.size() > 8192U) return false;
    kb::core::JsonValue root;
    std::string error;
    if (!kb::core::JsonValue::Parse(json, root, error) || root.GetKind() != kb::core::JsonValue::Kind::Object) return false;
    const auto* address = root.Find("address");
    const auto* port = root.Find("port");
    if (!address || address->GetKind() != kb::core::JsonValue::Kind::String || !port) return false;
    const double number = port->AsNumber(-1.0);
    if (!std::isfinite(number) || number < 1.0 || number > 65535.0 || std::floor(number) != number) return false;
    const std::string& text = address->AsString();
    std::uint32_t ipv4 = 0U;
    if (!ParseIpv4Literal(text, ipv4)) return false;
    SceneClientOptions parsed;
    if (!ParseSessionIdentity(root, parsed)) return false;
    parsed.server = Endpoint{ipv4, static_cast<std::uint16_t>(number)};
    options = std::move(parsed);
    return true;
}

struct SceneClient::Impl {
    Impl(kb::scene::Scene& value, ScriptNetworkRuntime& runtime, SceneClientOptions settings, TransportFactory create)
        : scene(value), script(runtime), options(std::move(settings)), factory(std::move(create)) {
        static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Client));
        script.SetClientAuthorized(false);
    }
    ~Impl() {
        if (replication) replication->HandleEvent(NetEvent{.type = NetEventType::Disconnected});
        if (client) client->Disconnect();
        if (authorized) script.HandleEvent(NetEvent{.type = NetEventType::Disconnected}); // the runtime outlives the session: its scripts hear OnNetDisconnected
        script.UnbindClient();
        static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Offline));
    }
    bool Connect(std::uint64_t now, std::ostream& diagnostics) {
        std::string error;
        auto next = factory(error);
        if (!next) { diagnostics << "network: transport creation failed\n"; return false; }
        script.SetClientAuthorized(false);
        if (replication) mismatches += replication->Stats().helloMismatch;
        replication.reset();
        client.reset();
        transport = std::move(next);
        ClientConfig config;
        config.identity = options.identity;
        if (config.identity.release == 0U && config.identity.contentHash == 0U) {
            if (const auto release = kb::security::CurrentVerifiedRelease()) {
                if (release->manifest.releaseNumber > std::numeric_limits<std::uint32_t>::max()) {
                    diagnostics << "network: release number exceeds the protocol limit\n";
                    return false;
                }
                options.identity = MakeBuildIdentity(static_cast<std::uint32_t>(release->manifest.releaseNumber), release->manifest.contentVersion);
                config.identity = options.identity;
            }
        }
        config.authorizationRequired = !options.token.empty();
        config.session.timeoutMilliseconds = 5000U;
        config.joinPassword = options.joinPassword;
        // A join without a client id is a script join: it uses the host's password admission, with an empty password when the game has none.
        config.admission = options.clientId == 0U || !options.joinPassword.empty() ? Admission::JoinPassword : Admission::KeyTable;
        if (config.admission == Admission::JoinPassword) {
            client = std::make_unique<Client>(*transport, config, options.server, now);
        } else {
            client = std::make_unique<Client>(*transport, config, options.clientId, options.key, options.server, now);
        }
        for (const auto& entry : client->Diagnostics().Entries()) {
            diagnostics << DiagName(entry.code) << ": " << entry.detail << '\n';
        }
        ReplicationConfig replicationConfig;
        replicationConfig.requestedBytesPerSecond = options.downlinkBytesPerSecond;
        replication = std::make_unique<ReplicationClient>(*client, scene.Runtime().EcsWorld(), *components, rpcs, replicationConfig);
        script.BindClient(*replication, *client);
        smoothed.clear();
        clock = NetworkClock{};
        auto variablesChanged = replication->SetStateCallback({});
        replication->SetStateCallback([this, variablesChanged = std::move(variablesChanged)](NetworkId id, kb::ecs::Entity entity, std::uint32_t tick) {
            if (variablesChanged) variablesChanged(id, entity, tick);
            const kb::scene::TransformComponent* transform = scene.Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(entity);
            if (transform == nullptr) return;
            if (replication->OwnerOf(id) == client->AssignedId()) return; // the player's own replica is not delayed (prediction owns it)
            clock.SetTickRate(replication->ServerTickRate());
            clock.OnState(tick, nowSeconds);
            auto found = smoothed.find(id);
            if (found == smoothed.end()) {
                found = smoothed.emplace(id, SnapshotInterpolator<kb::scene::TransformComponent>{ LerpTransform, clock.ViewDelaySeconds() }).first;
            }
            static_cast<void>(found->second.Add(clock.LocalTimeOf(tick), *transform));
        });
        authorized = false;
        disconnected = false;
        tokenPending = false;
        lastReplicas = 0U;
        diagnostics << "network: connecting\n";
        return true;
    }
    void Deliver(const NetEvent& event) {
        replication->HandleEvent(event);
        script.HandleEvent(event);
    }
    kb::scene::Scene& scene;
    ScriptNetworkRuntime& script;
    SceneClientOptions options;
    TransportFactory factory;
    std::unique_ptr<ReplicationRegistry> components;
    RpcRegistry rpcs;
    std::unique_ptr<IDatagramTransport> transport;
    std::unique_ptr<Client> client;
    std::unique_ptr<ReplicationClient> replication;
    bool initialized = false;
    bool authorized = false;
    bool tokenPending = false;
    bool disconnected = false;
    bool refused = false;
    std::uint32_t retries = 0U;
    std::uint64_t reconnectAt = 0U;
    std::size_t lastReplicas = 0U;
    std::uint64_t mismatches = 0U; // of the sessions that ended
    double nowSeconds = 0.0;
    // Each replica's server snapshots on the network clock, shown ViewDelaySeconds() in the past.
    std::map<NetworkId, SnapshotInterpolator<kb::scene::TransformComponent>> smoothed;
    NetworkClock clock;

    void Smooth() {
        for (auto it = smoothed.begin(); it != smoothed.end();) {
            const kb::ecs::Entity entity = replication ? replication->Find(it->first) : kb::ecs::Entity{};
            if (!entity.IsValid()) {
                it = smoothed.erase(it);
                continue;
            }
            it->second.SetDelay(clock.ViewDelaySeconds());
            if (const std::optional<kb::scene::TransformComponent> shown = it->second.Sample(nowSeconds)) {
                scene.Runtime().EcsWorld().Set(entity, *shown);
            }
            ++it;
        }
    }
};

SceneClient::SceneClient(kb::scene::Scene& scene, ScriptNetworkRuntime& script, SceneClientOptions options, TransportFactory factory)
    : impl_(std::make_unique<Impl>(scene, script, std::move(options), std::move(factory))) {}
SceneClient::~SceneClient() = default;

bool SceneClient::Connected() const noexcept {
    return impl_->authorized && !impl_->disconnected && impl_->client && impl_->client->State() == ClientState::Connected;
}

std::uint64_t SceneClient::DeclarationMismatches() const {
    return impl_->mismatches + (impl_->replication ? impl_->replication->Stats().helloMismatch : 0U);
}

std::size_t SceneClient::ReplicaCount() const {
    return impl_->replication ? impl_->replication->ReplicaCount() : 0U;
}

kb::ecs::Entity SceneClient::Replica(std::uint64_t networkId) const {
    return impl_->replication ? impl_->replication->Find(networkId) : kb::ecs::Entity{};
}

bool SceneClient::Update(std::uint64_t now, std::ostream& diagnostics) {
    auto& s = *impl_;
    if (!s.initialized) {
        if (!s.factory || !SceneTransformCodec{s.options.worldExtent}.Valid() ||
            s.options.token.size() > 1024U || s.options.reconnectAttempts > 10U || s.options.joinPassword.size() > 256U) {
            diagnostics << "network: invalid session settings\n";
            return false;
        }
        std::string error;
        s.components = BuildReplicationRegistry(s.scene.Runtime().EcsWorld(), s.script.Declarations(), s.options.worldExtent, error);
        if (!s.components) { diagnostics << "network: " << error << '\n'; return false; }
        DefineStandardRpcs(s.rpcs);
        if (!s.script.Freeze(s.rpcs, error)) { diagnostics << "network: RPC registry rejected\n"; return false; }
        s.initialized = true;
    }
    if (!s.client && !s.Connect(now, diagnostics)) return false;
    if (s.disconnected) {
        if (s.refused || s.retries >= s.options.reconnectAttempts) return true;
        if (now < s.reconnectAt) return true;
        ++s.retries;
        if (!s.Connect(now, diagnostics)) return false;
    }
    s.nowSeconds = static_cast<double>(now) / 1000.0;
    s.client->Update(now);
    NetEvent event;
    while (s.client->PollEvent(event)) {
        if (event.type == NetEventType::Connected) {
            if (!s.options.token.empty()) { s.tokenPending = true; continue; }
            s.authorized = true;
            s.retries = 0U;
            s.script.SetClientAuthorized(true);
            s.Deliver(event);
            diagnostics << "network: connected\n";
        } else if (event.type == NetEventType::Disconnected) {
            s.authorized = false;
            s.script.SetClientAuthorized(false);
            s.Deliver(event);
            s.disconnected = true;
            s.reconnectAt = now + 1000U;
            diagnostics << "network: disconnected\n";
        } else if (!s.options.token.empty() && !event.message.data.empty() &&
                   event.message.data[0] >= kAuthMessageAccepted && event.message.data[0] <= kAuthMessageKicked) {
            const auto& data = event.message.data;
            if (data[0] == kAuthMessageAccepted && data.size() == 5U && (data[1] & kScopePlay) != 0U) {
                if (!s.authorized) {
                    s.authorized = true;
                    s.retries = 0U;
                    s.script.SetClientAuthorized(true);
                    s.Deliver(NetEvent{.type = NetEventType::Connected});
                    diagnostics << "network: authorized\n";
                }
            } else {
                // Only a denied command stops automatic reconnects.
                const std::uint32_t value = data.size() == 5U ? (static_cast<std::uint32_t>(data[1]) | static_cast<std::uint32_t>(data[2]) << 8U |
                    static_cast<std::uint32_t>(data[3]) << 16U | static_cast<std::uint32_t>(data[4]) << 24U) : 0xFFFFFFFFU;
                const bool expired = (data[0] == kAuthMessageKicked && value == static_cast<std::uint32_t>(AuthKickReason::Expired)) ||
                    (data[0] == kAuthMessageRefused && value == static_cast<std::uint32_t>(AuthStatus::Expired));
                s.refused = data[0] == kAuthMessageDenied;
                if (expired) s.script.NotifyAuthExpired();
                s.authorized = false;
                s.script.SetClientAuthorized(false);
                s.client->Disconnect();
                diagnostics << "network: authorization refused\n";
            }
        } else if (s.authorized) {
            s.Deliver(event);
        }
    }
    if (s.tokenPending && s.client->State() == ClientState::Connected) {
        if (s.client->Authenticate(s.options.token) == SendResult::Queued) s.tokenPending = false;
    }
    if (s.authorized) {
        s.replication->Flush(now);
        s.Smooth();
        const auto count = s.replication->ReplicaCount();
        if (count != s.lastReplicas) {
            diagnostics << "network: replicas=" << count << '\n';
            s.lastReplicas = count;
        }
    }
    return true;
}
} // namespace kb::net

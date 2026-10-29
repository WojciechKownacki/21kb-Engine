#include "kb/net/SceneClient.hpp"
#include "kb/net/Auth.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/core/JsonValue.hpp"

#include <charconv>
#include <cmath>
#include <ostream>

namespace kb::net {
bool ParseWebSceneClientSettings(std::string_view json, SceneClientOptions& options, std::string& url) {
    if (json.size() > 8192U) return false;
    kb::core::JsonValue root;
    std::string error;
    if (!kb::core::JsonValue::Parse(json, root, error) || root.GetKind() != kb::core::JsonValue::Kind::Object) return false;
    const auto* endpoint = root.Find("url");
    const auto* id = root.Find("clientId");
    const auto* key = root.Find("key");
    if (!endpoint || !id || !key || endpoint->GetKind() != kb::core::JsonValue::Kind::String ||
        id->GetKind() != kb::core::JsonValue::Kind::String || key->GetKind() != kb::core::JsonValue::Kind::String) return false;
    SceneClientOptions parsed;
    const auto& text = id->AsString();
    const auto converted = std::from_chars(text.data(), text.data() + text.size(), parsed.clientId);
    if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || parsed.clientId == 0U) return false;
    const auto& address = endpoint->AsString();
    if (address.size() > 2048U || (!address.starts_with("ws://") && !address.starts_with("wss://")) ||
        address.find_first_of("\r\n") != std::string::npos || address.find('\0') != std::string::npos) return false;
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
        if (!std::isfinite(value) || value <= 0.0 || value > 1.0e9) return false;
        parsed.worldExtent = static_cast<float>(value);
    }
    if (const auto* attempts = root.Find("reconnectAttempts")) {
        const double value = attempts->AsNumber(-1.0);
        if (!std::isfinite(value) || value < 0.0 || value > 10.0 || std::floor(value) != value) return false;
        parsed.reconnectAttempts = static_cast<std::uint32_t>(value);
    }
    parsed.server = Endpoint::Loopback(1U);
    options = std::move(parsed);
    url = address;
    return true;
}

struct SceneClient::Impl {
    Impl(kb::scene::Scene& value, SceneClientOptions settings, TransportFactory create)
        : scene(value), options(std::move(settings)), factory(std::move(create)),
          script(ScriptNetworkRuntime::Role::Client, value) {
        script.SetClientAuthorized(false);
    }
    ~Impl() {
        if (replication) replication->HandleEvent(NetEvent{.type = NetEventType::Disconnected});
        if (client) client->Disconnect();
    }
    bool Connect(std::uint64_t now, std::ostream& diagnostics) {
        std::string error;
        auto next = factory(error);
        if (!next) { diagnostics << "network: transport creation failed\n"; return false; }
        script.SetClientAuthorized(false);
        replication.reset();
        client.reset();
        transport = std::move(next);
        ClientConfig config;
        config.authorizationRequired = !options.token.empty();
        config.session.timeoutMilliseconds = 5000U;
        client = std::make_unique<Client>(*transport, config, options.clientId, options.key, options.server, now);
        replication = std::make_unique<ReplicationClient>(*client, scene.Runtime().EcsWorld(), *components, rpcs);
        script.BindClient(*replication, *client, options.clientId);
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
    SceneClientOptions options;
    TransportFactory factory;
    ScriptNetworkRuntime script;
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
};

SceneClient::SceneClient(kb::scene::Scene& scene, SceneClientOptions options, TransportFactory factory)
    : impl_(std::make_unique<Impl>(scene, std::move(options), std::move(factory))) {}
SceneClient::~SceneClient() = default;

bool SceneClient::Update(std::uint64_t now, std::ostream& diagnostics) {
    auto& s = *impl_;
    if (!s.initialized) {
        if (s.options.clientId == 0U || !s.factory || !std::isfinite(s.options.worldExtent) ||
            s.options.worldExtent <= 0.0F || s.options.token.size() > 1024U || s.options.reconnectAttempts > 10U) {
            diagnostics << "network: invalid session settings\n";
            return false;
        }
        s.components = MakeSceneReplicationRegistry(s.options.worldExtent);
        DefineStandardRpcs(s.rpcs);
        std::string error;
        if (!s.script.Freeze(s.rpcs, error)) { diagnostics << "network: RPC registry rejected\n"; return false; }
        s.initialized = true;
        if (!s.Connect(now, diagnostics)) return false;
    }
    if (s.disconnected) {
        if (s.refused || s.retries >= s.options.reconnectAttempts) return true;
        if (now < s.reconnectAt) return true;
        ++s.retries;
        if (!s.Connect(now, diagnostics)) return false;
    }
    s.client->Update(now);
    NetEvent event;
    while (s.client->PollEvent(event)) {
        if (event.type == NetEventType::Connected) {
            if (!s.options.token.empty()) { s.tokenPending = true; continue; }
            s.authorized = true;
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
                    s.script.SetClientAuthorized(true);
                    s.Deliver(NetEvent{.type = NetEventType::Connected});
                    diagnostics << "network: authorized\n";
                }
            } else {
                s.refused = true;
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
        s.replication->Flush();
        const auto count = s.replication->ReplicaCount();
        if (count != s.lastReplicas) {
            diagnostics << "network: replicas=" << count << '\n';
            s.lastReplicas = count;
        }
    }
    return true;
}
} // namespace kb::net

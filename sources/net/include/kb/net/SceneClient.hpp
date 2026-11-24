#pragma once

#include "engine/ecs/Entity.hpp"
#include "kb/net/Host.hpp"
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace kb::scene { class Scene; }
namespace kb::net {

struct SceneClientOptions {
    BuildIdentity identity{};
    std::uint64_t clientId = 0U;
    SessionKey key{};
    Endpoint server{};
    std::string token;
    float worldExtent = 1000.0F;
    std::uint32_t reconnectAttempts = 3U;
#if defined(__ANDROID__)
    std::uint32_t downlinkBytesPerSecond = 49152U;
#else
    std::uint32_t downlinkBytesPerSecond = 131072U;
#endif
    // Web only: "transport": "webrtc" sends the datagrams over WebRTC data channels; `url` is then the
    // signaling WebSocket and `iceServers` lists the STUN and TURN servers (see WebRtcTransport.hpp).
    bool webRtc = false;
    std::vector<std::string> iceServers;
};

// Runtime-only Web settings. IDs are decimal strings to preserve all 64 bits.
[[nodiscard]] bool ParseWebSceneClientSettings(std::string_view json,
    SceneClientOptions& options, std::string& url);

// Runtime-only UDP settings: literal IPv4 address, numeric port and the same
// string identity/session credentials as the browser client. Failure leaves options unchanged.
[[nodiscard]] bool ParseNativeSceneClientSettings(std::string_view json, SceneClientOptions& options);

// Attach before the first script update. Update after scripts have registered
// their RPCs; subsequent updates pump replication and bounded reconnects.
class SceneClient final {
public:
    using TransportFactory = std::function<std::unique_ptr<IDatagramTransport>(std::string&)>;
    SceneClient(kb::scene::Scene& scene, SceneClientOptions options, TransportFactory factory);
    ~SceneClient();
    SceneClient(const SceneClient&) = delete;
    SceneClient& operator=(const SceneClient&) = delete;
    [[nodiscard]] bool Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics);
    // The session is up and the scripts may call the server.
    [[nodiscard]] bool Connected() const noexcept;
    // The replicas this client holds, and the one for a network id (invalid when it has none).
    [[nodiscard]] std::size_t ReplicaCount() const;
    [[nodiscard]] kb::ecs::Entity Replica(std::uint64_t networkId) const;
    // How many times the server's declarations (components, RPCs) differed from this client's, so that the client left: the scripts of the two sides differ.
    [[nodiscard]] std::uint64_t DeclarationMismatches() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace kb::net

#pragma once

#include "engine/ecs/Entity.hpp"
#include "kb/net/Host.hpp"
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>

namespace kb::scene { class Scene; }
namespace kb::net {

class ScriptNetworkRuntime;

struct SceneClientOptions {
    BuildIdentity identity{};
    std::uint64_t clientId = 0U;
    SessionKey key{};
    Endpoint server{};
    std::string token;
    std::string joinPassword; // non-empty: join with the game's password (the host assigns the player id; clientId and key are not used)
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

// A literal IPv4 address ("192.168.1.20"); false for anything else, 0.0.0.0 and 255.255.255.255.
[[nodiscard]] bool ParseIpv4Literal(std::string_view text, std::uint32_t& address);

// Attach before the first script update. Update after scripts have registered
// their RPCs; subsequent updates pump replication and bounded reconnects.
class SceneClient final {
public:
    using TransportFactory = std::function<std::unique_ptr<IDatagramTransport>(std::string&)>;
    SceneClient(kb::scene::Scene& scene, ScriptNetworkRuntime& script, SceneClientOptions options, TransportFactory factory);
    ~SceneClient();
    SceneClient(const SceneClient&) = delete;
    SceneClient& operator=(const SceneClient&) = delete;
    [[nodiscard]] bool Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics);
    // The application went to the background: tells the server (the session's three notices) and stops reconnecting
    // until ResumeNow. Replicas and the join settings stay, so the foreground resumes the same game.
    void Suspend(std::uint64_t nowMilliseconds);
    // The application came back: the next Update starts a new handshake at once instead of waiting for the retry delay.
    void ResumeNow(std::uint64_t nowMilliseconds);
    // The device has (or lost) a network. Without one no attempt is made; a network coming back drops the old session
    // and negotiates a new one at once (the server replaces the session of the same id).
    void SetNetworkAvailable(bool available, std::uint64_t nowMilliseconds);
    // The session is up and the scripts may call the server.
    [[nodiscard]] bool Connected() const noexcept;
    // The player's movement for this frame. The character moves at once on this machine and the input goes to the
    // server, which steps the same input and answers with its state (Net.Move).
    void Move(float vx, float vz, float jump, float dtSeconds);
    // How many server corrections moved the predicted character further than the tolerance.
    [[nodiscard]] std::uint64_t PredictionCorrections() const;
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

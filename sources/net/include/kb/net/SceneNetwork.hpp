#pragma once

#include "kb/net/LanDiscovery.hpp"
#include "kb/net/SceneClient.hpp"

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace kb::scene { class Scene; }
namespace kb::network { struct ScriptHostRequest; }
namespace kb::net {

struct SceneNetworkOptions {
    // Defaults of every join: identity, world extent, downlink, reconnects. A script join fills the server and the password.
    SceneClientOptions defaults{};
    // Creates the datagram transport of a session (UDP on native hosts, WebSocket or WebRTC in the browser).
    SceneClient::TransportFactory transport;
    // False where the transport is bound to one server (the browser): Net.Join is refused by name.
    bool scriptJoin = true;
    // Opens the listener of a hosted game on the given port (UDP on native hosts); empty where this build cannot host (the browser).
    std::function<std::unique_ptr<IDatagramTransport>(std::uint16_t port, std::string& error)> listen;
    // Opens a datagram transport for local network discovery (UDP with broadcast on native hosts); empty where this build
    // cannot look for games on the local network (the browser).
    std::function<std::unique_ptr<IDatagramTransport>(std::uint16_t port, bool broadcast, std::string& error)> lan;
    // Android only: holds the platform's multicast lock while local network discovery is on (empty elsewhere, where
    // broadcast reaches the network without one).
    std::function<void(bool)> multicastLock;
    std::uint32_t tickRate = 30U;
};

// The network of one scene for its whole life: one script runtime (offline, then a client after a join) and at most one session.
// Hosts create it with the scene and call Update after the scene's scripts.
class SceneNetwork final {
public:
    SceneNetwork(kb::scene::Scene& scene, SceneNetworkOptions options);
    ~SceneNetwork();
    SceneNetwork(const SceneNetwork&) = delete;
    SceneNetwork& operator=(const SceneNetwork&) = delete;
    // Joins with full client settings (a settings file, the browser's settings). False with `error` while a session exists.
    bool RequestJoin(SceneClientOptions options, std::string& error);
    // Hosts a game: this scene becomes the server and its player is player 1. False with `error` while a session exists or where
    // this build cannot host (the browser).
    bool RequestHost(const kb::network::ScriptHostRequest& request, std::string& error);
    // Turns looking for games on the local network on or off. False with `error` where this build cannot look.
    bool RequestDiscover(bool on, std::string& error);
    // The games seen on the local network (empty while discovery is off).
    [[nodiscard]] std::vector<LanGame> LanGames() const;
    void Leave() noexcept;
    // Starts a requested join, ends a requested leave, pumps the session. False when the session failed (as SceneClient::Update).
    [[nodiscard]] bool Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics);
    // The application went to the background: a client tells its server and stops until the foreground returns; a host
    // keeps its game open (the system stops its loop anyway and the guests time out on their own).
    void OnAppBackground();
    // The application came back: a client that is not connected negotiates again at once.
    void OnAppForeground();
    // The device gained or lost its network: without one no attempt is made; a new one negotiates again at once.
    void OnNetworkChanged(bool available);
    [[nodiscard]] bool Connected() const noexcept;
    [[nodiscard]] std::size_t ReplicaCount() const;
    [[nodiscard]] std::uint64_t DeclarationMismatches() const;
    [[nodiscard]] kb::ecs::Entity Replica(std::uint64_t networkId) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::net

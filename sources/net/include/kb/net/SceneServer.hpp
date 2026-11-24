#pragma once

// The network side of a server scene: transport, host, authorization, replication and the server's RPCs, on a scene that
// someone else owns and steps (the dedicated server process today, a hosting player's game next). The script runtime must
// be in the server role. The listener is opened by the caller and handed over.

#include "kb/net/Auth.hpp"
#include "kb/net/Host.hpp"
#include "kb/net/Replication.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kb::scene { class Scene; }
namespace kb::network { struct ReplicationDeclarations; }

namespace kb::net {

class ScriptNetworkRuntime;

enum class ReplicateMode : std::uint8_t {
    None,   // nothing is replicated by the server itself (scripts may mark entities)
    All,    // every entity with a transform
    Prefix, // entities whose name starts with `replicatePrefix`
};

struct SceneServerOptions {
    std::size_t maxClients = 16U;
    std::uint32_t maxConnectsPerSecondPerAddress = 8U;
    std::uint32_t maxSessionsPerAddress = 16U;
    std::uint32_t tickRate = 30U;
    std::uint32_t minClientRate = 16384U, maxClientRate = 262144U;
    std::uint32_t idleTimeoutSeconds = 30U;
    ReplicateMode replicate = ReplicateMode::All;
    std::string replicatePrefix;
    float worldExtent = 1000.0F;
    float visibilityRadius = 0.0F;
    GridPlane gridPlane = GridPlane::XZ;
    BuildIdentity identity{};
    Admission admission = Admission::KeyTable;
    std::string joinPassword;
    std::filesystem::path authProviderKeyFile;
    std::filesystem::path authRevocationsFile;
};

// The network side of a server scene: transport, host, authorization, replication and the server's RPCs, on a scene that
// someone else owns and steps (the dedicated server process today, a hosting player's game next). The script runtime must
// be in the server role.
class SceneServer final {
public:
    using Log = std::function<void(const std::string&)>;
    // The listener is opened by the caller (UDP, WebSocket or WebRTC on a dedicated server; UDP or loopback for a hosting
    // player or a test) and its bound port is known to the caller before this call. `keys` must outlive the server.
    SceneServer(kb::scene::Scene& scene, ScriptNetworkRuntime& net, const kb::network::ReplicationDeclarations& declarations,
        const IKeyProvider& keys, std::unique_ptr<IDatagramTransport> transport, SceneServerOptions options, Log log);
    ~SceneServer();
    SceneServer(const SceneServer&) = delete;
    SceneServer& operator=(const SceneServer&) = delete;
    // Builds the replication registry, freezes the scripts' RPCs, creates the host, replication and authorization.
    [[nodiscard]] bool Start(std::string& error);
    // Host events (and authorization commands) up to `nowMilliseconds`: call before the scene's step.
    void Pump(std::uint64_t nowMilliseconds);
    // After the scene's step: replicates the entities that appeared, then one replication tick.
    void Tick(std::uint32_t tick, std::uint64_t nowMilliseconds);
    [[nodiscard]] Host& NetHost() noexcept;
    [[nodiscard]] ReplicationServer& Replication() noexcept;
    // The listener this server owns; the caller may wait on it between ticks.
    [[nodiscard]] IDatagramTransport& Transport() noexcept;
    // The authorization gate, or nullptr when no provider key was configured.
    [[nodiscard]] AuthGate* Authorization() noexcept;
    // Peers that completed authorization; zero without a gate.
    [[nodiscard]] std::size_t AuthorizedPeers() const noexcept;
    [[nodiscard]] bool ProfileMetrics() const noexcept; // KB_NET_PROFILE was set

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::net

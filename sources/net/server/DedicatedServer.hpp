#pragma once

#include "kb/net/Replication.hpp"
#include "kb/net/Auth.hpp"
#include "kb/net/Rpc.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

// The dedicated server: loads a project and its startup scene without a window or a renderer, runs the
// scene on a fixed step, listens on a UDP port, and replicates the scene's entities to authenticated
// clients through kb_net. Command line parsing is in ServerMain.cpp; this is the part tests can reach.
namespace kb::net::server {

enum class ReplicateMode : std::uint8_t {
    None,   // nothing is replicated by the server itself (scripts may mark entities)
    All,    // every entity with a transform
    Prefix, // entities whose name starts with `replicatePrefix`
};

struct ServerOptions {
    std::filesystem::path project;
    std::string scene;                // empty: the project's startup scene
    std::string bindAddress = "0.0.0.0";
    std::uint16_t port = 27015U;      // 0: let the system choose; the port is written to portFile
    std::filesystem::path keysFile;   // lines of "<client id> <64 hex digits>"; '#' starts a comment
    std::size_t maxClients = 16U;
    std::uint32_t tickRate = 30U;     // fixed simulation and replication steps per second
    std::uint32_t idleTimeoutSeconds = 30U;  // a client silent for this long is dropped
    std::uint32_t emptyTimeoutSeconds = 0U;  // stop after this long without a client once one has been there (0: never)
    std::uint32_t maxSeconds = 0U;           // stop after this long (0: run until told to stop)
    ReplicateMode replicate = ReplicateMode::All;
    std::string replicatePrefix;
    float worldExtent = 1000.0F;
    std::filesystem::path stopFile;   // the server stops cleanly when this file exists
    std::filesystem::path portFile;   // the bound port, written once the socket is open
    std::filesystem::path statusFile; // the status JSON, rewritten about once a second
    std::filesystem::path stateFile;  // optional scene checkpoint, loaded on startup and saved on clean shutdown
    std::filesystem::path authProviderKeyFile; // optional Ed25519 public key, 64 hex digits
    std::filesystem::path authRevocationsFile; // optional signed snapshot, reloaded once per second
    bool quiet = false;
};

struct ServerSummary {
    std::string exitReason;           // stop-file, signal, max-seconds, empty-timeout, error
    std::uint64_t ticks = 0U;
    std::uint64_t uptimeMilliseconds = 0U;
    std::size_t clientsNow = 0U;
    std::size_t clientsPeak = 0U;
    std::size_t replicatedEntities = 0U;
    std::uint16_t port = 0U;
    bool stateLoaded = false;
    bool stateSaved = false;
    std::size_t authenticatedClients = 0U;
    AuthGate::Stats authorization;
    HostStats host;
    ReplicationServerStats replication;
};

// Parses key file text; returns false with a message naming the line.
[[nodiscard]] bool ParseKeysFile(const std::string& text, StaticKeyTable& keys, std::size_t& count, std::string& error);
[[nodiscard]] std::string SummaryJson(const ServerOptions& options, const ServerSummary& summary);

class DedicatedServer final {
public:
    DedicatedServer(ServerOptions options, std::ostream& log);
    ~DedicatedServer();
    DedicatedServer(const DedicatedServer&) = delete;
    DedicatedServer& operator=(const DedicatedServer&) = delete;

    // Loads the project and the scene, reads the keys and opens the socket. False with `error` set on any failure.
    [[nodiscard]] bool Start(std::string& error);
    // Runs until `stop` is set, a stop condition of the options holds, or an error. Returns the process exit code
    // (0 for a clean stop). Start must have succeeded.
    [[nodiscard]] int Run(const std::atomic<bool>& stop);
    [[nodiscard]] const ServerSummary& Summary() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace kb::net::server

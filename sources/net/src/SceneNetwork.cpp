#include "kb/net/SceneNetwork.hpp"
#include "kb/net/ProjectScene.hpp"
#include "kb/net/SceneServer.hpp"
#include "kb/net/ScriptBridge.hpp"
#include "engine/network/ReplicationDeclarations.hpp"
#include "engine/scene/Scene.hpp"

#include <algorithm>
#include <optional>
#include <ostream>
#include <utility>

namespace kb::net {

struct SceneNetwork::Impl {
    Impl(kb::scene::Scene& value, SceneNetworkOptions settings)
        : scene(value), options(std::move(settings)), declarationError(),
          script(ScriptNetworkRuntime::Role::Offline, scene, LoadDeclarations(scene, declarationError)) {
        script.SetSessionHandlers(
            [this](const kb::network::ScriptJoinRequest& request, std::string& error) { return JoinFromScript(request, error); },
            [this] { Leave(); },
            [this](const kb::network::ScriptHostRequest& request, std::string& error) { return HostFromScript(request, error); },
            [this](bool on, std::string& error) { return RequestDiscover(on, error); });
    }
    static kb::network::ReplicationDeclarations LoadDeclarations(kb::scene::Scene& scene, std::string& error) {
        kb::network::ReplicationDeclarations result;
        static_cast<void>(LoadReplicationDeclarations(scene, result, error));
        return result;
    }
    bool JoinFromScript(const kb::network::ScriptJoinRequest& request, std::string& error) {
        if (!options.scriptJoin) {
            error = "this build joins only the server it was started for";
            return false;
        }
        std::uint32_t ip = 0U;
        if (!ParseIpv4Literal(request.address, ip)) {
            error = "address must be a literal IPv4 address";
            return false;
        }
        SceneClientOptions copy = options.defaults;
        copy.server = Endpoint{ ip, request.port };
        copy.joinPassword = request.password;
        return RequestJoin(std::move(copy), error);
    }
    bool RequestJoin(SceneClientOptions settings, std::string& error) {
        if (client || pendingJoin) {
            error = "already in a session";
            return false;
        }
        if (settings.identity.release == 0U && settings.identity.contentHash == 0U) {
            settings.identity = options.defaults.identity;
        }
        pendingJoin = std::move(settings);
        return true;
    }
    bool HostFromScript(const kb::network::ScriptHostRequest& request, std::string& error) {
        if (client || server || pendingJoin || pendingHost) {
            error = "already in a session";
            return false;
        }
        if (!options.listen) {
            error = "this build cannot host a game";
            return false;
        }
        if (request.name.size() > kLanGameNameBytes) {
            error = "the game name must be at most 32 bytes of UTF-8";
            return false;
        }
        pendingHost = request;
        return true;
    }
    bool RequestDiscover(bool on, std::string& error) {
        if (!on) {
            for (const LanGame& game : announcedGames) {
                script.NotifyLanGameLost(game.address, game.port);
            }
            announcedGames.clear();
            browser.reset();
            browserTransport.reset();
            return true;
        }
        if (browser) {
            return true;
        }
        if (!options.lan) {
            error = "this build cannot look for games on the local network";
            return false;
        }
        browserTransport = options.lan(0U, true, error);
        if (!browserTransport) {
            if (error.empty()) {
                error = "the discovery socket could not be opened";
            }
            return false;
        }
        browser = std::make_unique<LanBrowser>(*browserTransport, options.defaults.identity.contentHash);
        return true;
    }
    [[nodiscard]] std::vector<LanGame> LanGames() const { return browser ? browser->Games() : std::vector<LanGame>{}; }
    void ReportLanGames() {
        if (!browser) {
            return;
        }
        const std::vector<LanGame>& current = browser->Games();
        const auto matches = [](const LanGame& game, const LanGame& other) {
            return game.address.address == other.address.address && game.port == other.port;
        };
        for (const LanGame& game : announcedGames) {
            if (std::none_of(current.begin(), current.end(), [&game, &matches](const LanGame& seen) { return matches(game, seen); })) {
                script.NotifyLanGameLost(game.address, game.port);
            }
        }
        for (const LanGame& game : current) {
            if (std::none_of(announcedGames.begin(), announcedGames.end(), [&game, &matches](const LanGame& known) { return matches(game, known); })) {
                script.NotifyLanGameFound(game);
            }
        }
        announcedGames = current;
    }
    void Leave() noexcept {
        pendingLeave = client != nullptr || pendingJoin.has_value() || server != nullptr || pendingHost.has_value();
        pendingJoin.reset();
        pendingHost.reset();
    }
    bool Update(std::uint64_t now, std::ostream& diagnostics) {
        diagnosticStream = &diagnostics;
        script.SetNow(now);
        if (pendingLeave) {
            client.reset();
            if (server) {
                for (const std::uint64_t id : server->NetHost().ClientIds()) {
                    server->NetHost().Disconnect(id);
                }
                server->Pump(now); // sends the close notices before the listener goes away
                server.reset();
                lanBeacon.reset();
                beaconTransport.reset();
                script.UnbindServer();
                static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Offline));
            }
            pendingLeave = false;
        }
        if (pendingHost && !client && !server) {
            if (!declarationError.empty()) {
                diagnostics << "network: " << declarationError << '\n';
                pendingHost.reset();
                return false;
            }
            const kb::network::ScriptHostRequest request = *pendingHost;
            std::string error;
            std::unique_ptr<IDatagramTransport> transport = options.listen(request.port, error);
            if (transport == nullptr) {
                diagnostics << "network: cannot host: " << error << '\n';
                pendingHost.reset();
                return false;
            }
            static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Host));
            SceneServerOptions hosted;
            hosted.maxClients = request.maxPlayers;
            hosted.tickRate = options.tickRate;
            hosted.identity = options.defaults.identity;
            hosted.admission = Admission::JoinPassword;
            hosted.joinPassword = request.password;
            hosted.worldExtent = options.defaults.worldExtent;
            pendingHost.reset();
            server = std::make_unique<SceneServer>(scene, script, script.Declarations(), noKeys, std::move(transport), std::move(hosted),
                [this](const std::string& message) {
                    if (diagnosticStream != nullptr) *diagnosticStream << "network: " << message << '\n';
                });
            if (!server->Start(error)) {
                diagnostics << "network: cannot host: " << error << '\n';
                server.reset();
                script.UnbindServer();
                static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Offline));
                return false;
            }
            if (options.lan) {
                std::string lanError;
                std::unique_ptr<IDatagramTransport> discovered = options.lan(kLanDiscoveryPort, true, lanError);
                if (discovered == nullptr) {
                    diagnostics << "network: local network discovery is off: " << lanError << '\n';
                } else {
                    LanBeaconOptions beacon;
                    beacon.gameKey = options.defaults.identity.contentHash;
                    beacon.hostPort = server->Transport().LocalEndpoint().port;
                    beacon.maxPlayers = static_cast<std::uint16_t>(std::min<std::uint32_t>(request.maxPlayers, 65535U));
                    beacon.passwordRequired = !request.password.empty();
                    beacon.name = request.name;
                    const auto salt = server->NetHost().HostSalt();
                    for (std::size_t index = 0U; index < salt.size(); ++index) {
                        beacon.hostSalt |= static_cast<std::uint64_t>(salt[index]) << (8U * index);
                    }
                    beaconTransport = std::move(discovered);
                    lanBeacon = std::make_unique<LanBeacon>(*beaconTransport, std::move(beacon));
                }
            }
            nextTickAt = now;
        }
        if (pendingJoin && !client && !declarationError.empty()) {
            diagnostics << "network: " << declarationError << '\n';
            pendingJoin.reset();
            return false;
        }
        if (pendingJoin && !client) {
            client = std::make_unique<SceneClient>(scene, script, std::move(*pendingJoin), options.transport);
            pendingJoin.reset();
        }
        if (server) {
            server->Pump(now);
            if (now >= nextTickAt) {
                server->Tick(++tick, now);
                const std::uint32_t rate = options.tickRate == 0U ? 30U : options.tickRate;
                nextTickAt += 1000U / rate;
                if (nextTickAt <= now) {
                    nextTickAt = now + 1000U / rate;
                }
            }
        }
        if (browser) {
            browser->Update(now);
            ReportLanGames();
        }
        if (lanBeacon) {
            lanBeacon->SetPlayers(static_cast<std::uint16_t>(std::min<std::size_t>(script.PlayerCount(), 65535U)));
            lanBeacon->Update(now);
        }
        return client ? client->Update(now, diagnostics) : true;
    }

    kb::scene::Scene& scene;
    SceneNetworkOptions options;
    std::string declarationError;
    ScriptNetworkRuntime script;
    std::optional<SceneClientOptions> pendingJoin;
    std::optional<kb::network::ScriptHostRequest> pendingHost;
    bool pendingLeave = false;
    std::unique_ptr<SceneClient> client;
    StaticKeyTable noKeys;
    std::unique_ptr<SceneServer> server;
    std::unique_ptr<IDatagramTransport> browserTransport;
    std::unique_ptr<LanBrowser> browser;
    std::unique_ptr<IDatagramTransport> beaconTransport;
    std::unique_ptr<LanBeacon> lanBeacon;
    std::vector<LanGame> announcedGames;
    std::ostream* diagnosticStream = nullptr;
    std::uint32_t tick = 0U;
    std::uint64_t nextTickAt = 0U;
};

SceneNetwork::SceneNetwork(kb::scene::Scene& scene, SceneNetworkOptions options)
    : impl_(std::make_unique<Impl>(scene, std::move(options))) {}
SceneNetwork::~SceneNetwork() = default;

bool SceneNetwork::RequestJoin(SceneClientOptions options, std::string& error) {
    return impl_->RequestJoin(std::move(options), error);
}

bool SceneNetwork::RequestHost(const kb::network::ScriptHostRequest& request, std::string& error) {
    return impl_->HostFromScript(request, error);
}

bool SceneNetwork::RequestDiscover(bool on, std::string& error) {
    return impl_->RequestDiscover(on, error);
}

std::vector<LanGame> SceneNetwork::LanGames() const {
    return impl_->LanGames();
}

void SceneNetwork::Leave() noexcept {
    impl_->Leave();
}

bool SceneNetwork::Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics) {
    return impl_->Update(nowMilliseconds, diagnostics);
}

bool SceneNetwork::Connected() const noexcept {
    return (impl_->client != nullptr && impl_->client->Connected()) || impl_->server != nullptr;
}

std::size_t SceneNetwork::ReplicaCount() const {
    return impl_->client != nullptr ? impl_->client->ReplicaCount() : 0U;
}

std::uint64_t SceneNetwork::DeclarationMismatches() const {
    return impl_->client != nullptr ? impl_->client->DeclarationMismatches() : 0U;
}

kb::ecs::Entity SceneNetwork::Replica(std::uint64_t networkId) const {
    return impl_->client != nullptr ? impl_->client->Replica(networkId) : kb::ecs::Entity{};
}

} // namespace kb::net

#include "kb/net/SceneNetwork.hpp"
#include "kb/net/ProjectScene.hpp"
#include "kb/net/SceneServer.hpp"
#include "kb/net/ScriptBridge.hpp"
#include "engine/network/ReplicationDeclarations.hpp"
#include "engine/scene/Scene.hpp"

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
            [this](const kb::network::ScriptHostRequest& request, std::string& error) { return HostFromScript(request, error); });
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
        pendingHost = request;
        return true;
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
            std::string error;
            std::unique_ptr<IDatagramTransport> transport = options.listen(pendingHost->port, error);
            if (transport == nullptr) {
                diagnostics << "network: cannot host: " << error << '\n';
                pendingHost.reset();
                return false;
            }
            static_cast<void>(script.SetRole(ScriptNetworkRuntime::Role::Host));
            SceneServerOptions hosted;
            hosted.maxClients = pendingHost->maxPlayers;
            hosted.tickRate = options.tickRate;
            hosted.identity = options.defaults.identity;
            hosted.admission = Admission::JoinPassword;
            hosted.joinPassword = pendingHost->password;
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

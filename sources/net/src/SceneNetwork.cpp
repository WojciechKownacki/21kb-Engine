#include "kb/net/SceneNetwork.hpp"
#include "kb/net/ProjectScene.hpp"
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
            [this] { Leave(); });
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
    void Leave() noexcept {
        pendingLeave = client != nullptr || pendingJoin.has_value();
        pendingJoin.reset();
    }
    bool Update(std::uint64_t now, std::ostream& diagnostics) {
        if (pendingLeave) {
            client.reset();
            pendingLeave = false;
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
        return client ? client->Update(now, diagnostics) : true;
    }

    kb::scene::Scene& scene;
    SceneNetworkOptions options;
    std::string declarationError;
    ScriptNetworkRuntime script;
    std::optional<SceneClientOptions> pendingJoin;
    bool pendingLeave = false;
    std::unique_ptr<SceneClient> client;
};

SceneNetwork::SceneNetwork(kb::scene::Scene& scene, SceneNetworkOptions options)
    : impl_(std::make_unique<Impl>(scene, std::move(options))) {}
SceneNetwork::~SceneNetwork() = default;

bool SceneNetwork::RequestJoin(SceneClientOptions options, std::string& error) {
    return impl_->RequestJoin(std::move(options), error);
}

void SceneNetwork::Leave() noexcept {
    impl_->Leave();
}

bool SceneNetwork::Update(std::uint64_t nowMilliseconds, std::ostream& diagnostics) {
    return impl_->Update(nowMilliseconds, diagnostics);
}

bool SceneNetwork::Connected() const noexcept {
    return impl_->client != nullptr && impl_->client->Connected();
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

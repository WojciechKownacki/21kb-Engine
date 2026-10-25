// Gameplay scripts over the network: a server scene and two client scenes, each running the same Lua
// script, on a simulated network with loss, reordering and duplication. The script declares RPCs, marks
// an entity as replicated, reacts to connections, spawns and calls, and calls back.

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/script/ScriptNetApi.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace kb::net;
namespace fs = std::filesystem;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// One script for both sides; it asks Net.IsServer() which side it is on.
constexpr const char* kScript = R"lua(
function Ready(self, dt)
    CallFunction("Net.RegisterRpc", { name = "Move", direction = "toServer", args = "entity,float(-50..50),float(-50..50)", owner = 0 })
    CallFunction("Net.RegisterRpc", { name = "Score", direction = "toClients", args = "uint(0..1000)" })
    if CallFunction("Net.IsServer", {}) then
        CallFunction("Net.Replicate", { entity = self.entity })
    end
end

function OnNetConnected(self, event)
    if CallFunction("Net.IsServer", {}) then
        SetShared("server.connected." .. event.args.player, true)
        if GetShared("server.owner") == nil then
            SetShared("server.owner", event.args.player)
            CallFunction("Net.SetOwner", { entity = self.entity, player = event.args.player })
        end
    else
        SetShared("client.connected", event.args.player)
    end
end

function OnNetDisconnected(self, event)
    if not CallFunction("Net.IsServer", {}) then
        SetShared("client.disconnected", event.args.reason)
    end
end

function OnNetSpawned(self, event)
    SetShared("client.spawned", event.args.entity)
    SetShared("client.spawnOwner", event.args.owner)
    -- Every client tries to move the hero. Only its owner may.
    CallFunction("Net.Call", { rpc = "Move", e1 = event.args.entity, n1 = 3.0, n2 = 4.0 })
end

function OnRpc_Move(self, event)
    self:SetProperty("Transform", "localPosition.x", event.args.n1)
    self:SetProperty("Transform", "localPosition.y", event.args.n2)
    SetShared("server.moves", (GetShared("server.moves") or 0) + 1)
    SetShared("server.moveSender", event.args.sender)
    CallFunction("Net.Call", { rpc = "Score", target = event.args.sender, n1 = 10 })
end

function OnRpc_Score(self, event)
    SetShared("client.score", event.args.n1)
end
)lua";

SessionKey KeyFor(std::uint8_t id) {
    SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(id * 9U + index);
    }
    return key;
}

// A scene with the script on one entity, a script host, and the Net runtime for its role.
struct Side {
    Side(const fs::path& project, ScriptNetworkRuntime::Role role, const char* entityName, float x, float y, float z) {
        scene = std::make_unique<kb::scene::Scene>();
        Require(scene->Assets().MountProject(project) && scene->Assets().Discover() == 1U, "the project could not be mounted");
        const kb::assets::AssetMetadata* metadata = scene->Assets().Manager().Registry().FindByPath("/Game/Logic/Net.lua");
        Require(metadata != nullptr, "the script is not in the project");
        const kb::scene::SceneObject object = scene->Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = entityName });
        entity = object.Entity();
        kb::scene::TransformComponent transform = *scene->Transforms().TryGet(entity);
        transform.localPosition = { x, y, z };
        scene->Transforms().Set(entity, transform);
        scene->Components().Behaviours().Set(entity, kb::scene::BehaviourComponent{ .behaviourAssetId = metadata->id.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
        host = std::make_unique<kb::script::ScriptRuntimeHost>(*scene);
        Require(host->Succeeded(), "the script host failed");
        runtime = std::make_unique<ScriptNetworkRuntime>(role, *scene);
        Require(host->InstallSceneSystem(), "the script scene system was not installed"); // runs Created and Ready
        components = MakeSceneReplicationRegistry(1000.0F);
        rpcs = std::make_unique<RpcRegistry>();
        DefineStandardRpcs(*rpcs);
        std::string error;
        Require(runtime->Freeze(*rpcs, error), error);
    }

    [[nodiscard]] std::optional<kb::script::ScriptValue> Shared(const char* key) const { return host->SharedState().Get(key); }
    void Step() { static_cast<void>(scene->Runtime().Update(0.03F)); }

    std::unique_ptr<kb::scene::Scene> scene;
    kb::scene::SceneEntity entity;
    std::unique_ptr<kb::script::ScriptRuntimeHost> host;
    std::unique_ptr<ScriptNetworkRuntime> runtime;
    std::unique_ptr<ReplicationRegistry> components;
    std::unique_ptr<RpcRegistry> rpcs;
};

struct ClientSide {
    ClientSide(LoopbackNetwork& network, const fs::path& project, std::uint64_t id, std::uint16_t port, std::uint64_t now)
        : side(project, ScriptNetworkRuntime::Role::Client, "Controller", 0.0F, 0.0F, 0.0F), playerId(id) {
        transport = network.CreateTransport(Endpoint::Loopback(port));
        ClientConfig config;
        config.connectTimeoutMilliseconds = 60'000U;
        client = std::make_unique<Client>(*transport, config, id, KeyFor(static_cast<std::uint8_t>(id)), Endpoint::Loopback(9300), now);
        replicaWorld = std::make_unique<kb::ecs::World>();
        replication = std::make_unique<ReplicationClient>(*client, *replicaWorld, *side.components, *side.rpcs);
        side.runtime->BindClient(*replication, *client, id);
    }

    Side side;
    std::uint64_t playerId;
    std::unique_ptr<IDatagramTransport> transport;
    std::unique_ptr<Client> client;
    std::unique_ptr<kb::ecs::World> replicaWorld;
    std::unique_ptr<ReplicationClient> replication;
};

struct World {
    explicit World(const fs::path& project, LinkConditions conditions) : projectRoot(project), network(conditions), server(project, ScriptNetworkRuntime::Role::Server, "Hero", 1.0F, 2.0F, 3.0F) {
        hostTransport = network.CreateTransport(Endpoint::Loopback(9300));
        keys.Set(1U, KeyFor(1U));
        keys.Set(2U, KeyFor(2U));
        host = std::make_unique<Host>(*hostTransport, HostConfig{}, keys);
        replication = std::make_unique<ReplicationServer>(*host, server.scene->Runtime().EcsWorld(), *server.components, *server.rpcs);
        server.runtime->BindServer(*replication);
    }

    ClientSide& AddClient(std::uint64_t id, std::uint64_t now) {
        clients.push_back(std::make_unique<ClientSide>(network, projectRoot, id, static_cast<std::uint16_t>(9300U + id), now));
        return *clients.back();
    }

    void Step(std::uint64_t now) {
        network.Advance(now);
        host->Update(now);
        NetEvent event;
        while (host->PollEvent(event)) {
            server.runtime->HandleEvent(event);
            replication->HandleEvent(event, now);
        }
        for (auto& client : clients) {
            client->client->Update(now);
            while (client->client->PollEvent(event)) {
                client->side.runtime->HandleEvent(event);
                client->replication->HandleEvent(event);
            }
        }
        if (now >= nextTick) {
            nextTick = now + 30U;
            server.Step();
            replication->Tick(++tick, now);
            for (auto& client : clients) {
                client->side.Step();
            }
        }
        for (auto& client : clients) {
            client->replication->Flush();
        }
    }

    std::uint64_t Run(std::uint64_t from, std::uint64_t limit, const std::function<bool()>& done) {
        std::uint64_t now = from;
        for (; now < from + limit; now += 10U) {
            Step(now);
            if (done()) {
                break;
            }
        }
        return now;
    }

    fs::path projectRoot;
    LoopbackNetwork network;
    Side server;
    std::unique_ptr<IDatagramTransport> hostTransport;
    StaticKeyTable keys;
    std::unique_ptr<Host> host;
    std::unique_ptr<ReplicationServer> replication;
    std::vector<std::unique_ptr<ClientSide>> clients;
    std::uint32_t tick = 0U;
    std::uint64_t nextTick = 0U;
};

fs::path MakeProject() {
    const fs::path root = fs::temp_directory_path() / "21kb_net_script_test";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "Assets" / "Logic");
    std::ofstream script(root / "Assets" / "Logic" / "Net.lua", std::ios::binary | std::ios::trunc);
    script << kScript;
    return root;
}

LinkConditions BadLink() {
    LinkConditions conditions;
    conditions.simulation.latencyMilliseconds = 30U;
    conditions.simulation.jitterMilliseconds = 15U;
    conditions.simulation.lossPermille = 200U;
    conditions.simulation.reorderPermille = 150U;
    conditions.simulation.seed = 21U;
    conditions.duplicatePermille = 80U;
    return conditions;
}

double Number(const std::optional<kb::script::ScriptValue>& value) {
    if (!value.has_value()) {
        return -999.0;
    }
    switch (value->Type()) {
    case kb::script::ScriptValueType::Float: return static_cast<double>(value->AsFloat(-999.0F));
    case kb::script::ScriptValueType::Double: return value->AsDouble(-999.0);
    case kb::script::ScriptValueType::UInt32: return static_cast<double>(value->AsUInt32(0U));
    case kb::script::ScriptValueType::Int: return static_cast<double>(value->AsInt(-999));
    case kb::script::ScriptValueType::Int64: return static_cast<double>(value->AsInt64(-999));
    default: return static_cast<double>(value->AsUInt64(0U));
    }
}

void TestScriptsOverTheNetwork() {
    const fs::path project = MakeProject();
    World world(project, BadLink());

    // The server's scripts declared the RPCs and marked the hero before the session existed.
    Require(world.server.runtime->IsReplicated(world.server.entity.Id()), "the hero was not marked as replicated");
    Require(world.replication->ReplicatedCount() == 1U, "the marked entity was not replicated when the session started");
    Require(world.server.rpcs->Count() == 6U, "the server did not define the standard and the script RPCs");

    // The first player connects; the script gives the hero to them, they get the hero, move it, and are told their score.
    ClientSide& first = world.AddClient(1U, 0U);
    std::uint64_t now = world.Run(0U, 60'000U, [&] { return first.side.Shared("client.score").has_value(); });
    Require(first.side.Shared("client.score").has_value(), "the first player never got the score");
    Require(Number(first.side.Shared("client.connected")) == 1.0 && first.side.runtime->Connected() && first.side.runtime->LocalPlayerId() == 1U,
        "the client script cannot see its connection: connected=" + std::to_string(Number(first.side.Shared("client.connected"))) + " runtime=" +
            std::to_string(first.side.runtime->Connected()) + " id=" + std::to_string(first.side.runtime->LocalPlayerId()));
    Require(world.server.Shared("server.connected.1").has_value() && Number(world.server.Shared("server.owner")) == 1.0, "the server script did not see the player connect");
    Require(first.side.Shared("client.spawned").has_value() && first.replication->ReplicaCount() == 1U, "the client script did not see the hero spawn");
    Require(Number(first.side.Shared("client.score")) == 10.0, "the score has the wrong value");
    Require(Number(world.server.Shared("server.moves")) == 1.0 && Number(world.server.Shared("server.moveSender")) == 1.0, "the server did not run the move once, for player 1");
    const kb::scene::TransformComponent* hero = world.server.scene->Transforms().TryGet(world.server.entity);
    Require(hero != nullptr && hero->localPosition.x == 3.0F && hero->localPosition.y == 4.0F && hero->localPosition.z == 3.0F, "the hero is not where the script moved it");

    // The move reaches the client's replica.
    const auto replicaPosition = [&](ClientSide& client, float& x, float& y, float& z) {
        const kb::ecs::Entity replica = client.replication->Find(1U);
        const kb::scene::TransformComponent* transform = replica.IsValid() ? client.replicaWorld->TryGet<kb::scene::TransformComponent>(replica) : nullptr;
        if (transform == nullptr) {
            return false;
        }
        x = transform->localPosition.x;
        y = transform->localPosition.y;
        z = transform->localPosition.z;
        return true;
    };
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    now = world.Run(now, 30'000U, [&] { return replicaPosition(first, x, y, z) && std::fabs(x - 3.0F) < 0.05F && std::fabs(y - 4.0F) < 0.05F; });
    Require(replicaPosition(first, x, y, z) && std::fabs(x - 3.0F) < 0.05F && std::fabs(y - 4.0F) < 0.05F && std::fabs(z - 3.0F) < 0.05F, "the replica did not follow the server's hero");

    // A second player sees the hero too, tries to move it, and is refused: the hero is not theirs.
    ClientSide& second = world.AddClient(2U, now);
    now = world.Run(now, 60'000U, [&] { return second.side.Shared("client.spawned").has_value(); });
    Require(second.side.Shared("client.spawned").has_value(), "the second player did not see the hero");
    now = world.Run(now, 10'000U, [&] { return world.replication->Stats().rpcRejectedPermission >= 1U; });
    Require(world.replication->Stats().rpcRejectedPermission >= 1U, "the move of a player who does not own the hero was not refused");
    now = world.Run(now, 3000U, [] { return false; });
    Require(Number(world.server.Shared("server.moves")) == 1.0, "a refused move ran in the server script");
    Require(!second.side.Shared("client.score").has_value(), "the player who was refused got a score");
    hero = world.server.scene->Transforms().TryGet(world.server.entity);
    Require(hero->localPosition.x == 3.0F && hero->localPosition.y == 4.0F, "a refused move changed the hero");
    float sx = 0.0F;
    float sy = 0.0F;
    float sz = 0.0F;
    now = world.Run(now, 20'000U, [&] { return replicaPosition(second, sx, sy, sz) && std::fabs(sx - 3.0F) < 0.05F; });
    Require(replicaPosition(second, sx, sy, sz) && std::fabs(sx - 3.0F) < 0.05F && std::fabs(sy - 4.0F) < 0.05F, "the second player's replica is wrong");

    // A player leaves: the script hears that its own connection ended.
    first.client->Disconnect();
    now = world.Run(now, 10'000U, [&] { return first.side.Shared("client.disconnected").has_value(); });
    Require(first.side.Shared("client.disconnected").has_value() && first.side.Shared("client.disconnected")->AsString() == "requested", "the script was not told its connection ended");
    Require(first.side.runtime->PlayerCount() == 0U && !first.side.runtime->Connected(), "the runtime thinks it is still connected");
    std::printf("  scripts over the network: %llu RPC calls handled by the server, %llu refused for lack of permission\n",
        static_cast<unsigned long long>(world.replication->Stats().rpcDispatched), static_cast<unsigned long long>(world.replication->Stats().rpcRejectedPermission));
}

void TestRuntimeGuards() {
    const fs::path project = MakeProject();
    // A runtime for a scene that has no session: offline functions answer, calls explain.
    kb::scene::Scene scene;
    kb::script::ScriptRuntimeHost host{ scene };
    {
        ScriptNetworkRuntime runtime(ScriptNetworkRuntime::Role::Server, scene);
        std::string error;
        kb::network::ScriptRpcDeclaration declaration;
        declaration.name = "Hello";
        declaration.args.push_back(kb::network::ScriptRpcArgSpec{ kb::network::ScriptRpcArgKind::String, 0.0, 0.0, 16U });
        Require(runtime.DeclareRpc(declaration, error), error);
        Require(!runtime.DeclareRpc(declaration, error) && error == "already registered", "a repeated declaration was accepted");
        kb::network::ScriptRpcDeclaration bad;
        bad.name = "Bad";
        bad.ownerArg = 3;
        Require(!runtime.DeclareRpc(bad, error), "a declaration with an owner argument that does not exist was accepted");
        const std::vector<kb::network::ScriptRpcValue> args{ std::string("hi") };
        Require(!runtime.CallRpc("Hello", 0U, args, error) && error.find("goes from") != std::string::npos, "a server called a client-to-server RPC: " + error);
        RpcRegistry registry;
        Require(runtime.Freeze(registry, error) && registry.Count() == 1U, "the declarations did not reach the registry");
        Require(!runtime.DeclareRpc(declaration, error) && error.find("started") != std::string::npos, "a declaration after Freeze was accepted");
        Require(runtime.Replicate(kb::ecs::Entity{ 99U }.Id(), error) && runtime.IsReplicated(99U), "a replicate request before the session was not kept");
        Require(!runtime.SetOwner(99U, 1U, error), "an owner was set without a session");
        Require(kb::script::ScriptNetApi::Attached(scene) == &runtime, "the runtime did not attach itself");
    }
    Require(kb::script::ScriptNetApi::Attached(scene) == nullptr, "the runtime did not detach itself");
    static_cast<void>(project);
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "runtime-guards", &TestRuntimeGuards },
        { "scripts-over-the-network", &TestScriptsOverTheNetwork },
    };
    int failures = 0;
    for (const auto& [name, run] : tests) {
        std::printf("START %s\n", name);
        std::fflush(stdout);
        try {
            run();
            std::printf("PASS %s\n", name);
        } catch (const std::exception& error) {
            std::printf("FAIL %s: %s\n", name, error.what());
            ++failures;
        }
        std::fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}

#include "CliCommands.hpp"
#include "CliProgram.hpp"
#include "CliUiReport.hpp"

#include "engine/ecs/World.hpp"
#include "engine/input/InputDeviceState.hpp"
#include "engine/input/InputSubsystem.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/PhysicsBackend.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneInputActivation.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "engine/scene/SceneUI.hpp"
#include "engine/scene/TransformComponent.hpp"
#include "engine/script/ScriptNetApi.hpp"
#include "engine/script/ScriptRuntimeHost.hpp"
#include "kb/net/Host.hpp"
#include "kb/net/ProjectScene.hpp"
#include "kb/net/SceneClient.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ScriptBridge.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>

// net check: the server and the players of a project in one process, without a window and without a socket. The server and every client
// load the same project and run its scripts exactly as the dedicated server and the game do (kb::net::server, kb::net::SceneClient); the
// datagrams between them travel over the engine's simulated network, so a run is repeatable and takes no real time. The report says who
// connected, what the server replicated, whether the players see it, and, for the mistakes a multiplayer script usually has, what to change.
namespace kb::cli {
namespace {

constexpr float kStepSeconds = 1.0F / 30.0F;      // the server and each frame of the run
constexpr std::uint64_t kHalfStepMilliseconds = 17U;  // the players draw twice per step, as a player at 60 frames a second does
constexpr std::uint16_t kServerPort = 9300U;
constexpr std::size_t kMaxClients = 8U;
constexpr std::size_t kMaxLines = 120U;

[[nodiscard]] kb::net::SessionKey KeyFor(std::uint64_t id) {
    kb::net::SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) key[index] = static_cast<std::uint8_t>(id * 7U + index * 3U + 1U);
    return key;
}

[[nodiscard]] std::optional<int> ParseInt(std::string_view text) {
    int value = 0;
    const auto [end, code] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || code != std::errc{} || end != text.data() + text.size()) return std::nullopt;
    return value;
}

// "2:W@10+30" is the key W held by client 2 from frame 10 for 30 frames; without "2:" it is client 1's.
struct Targeted {
    std::size_t client = 1U;
    std::string_view rest;
};

[[nodiscard]] Targeted SplitClient(std::string_view text) {
    const std::size_t colon = text.find(':');
    const std::size_t at = text.find('@');
    if (colon != std::string_view::npos && (at == std::string_view::npos || colon < at)) {
        if (const std::optional<int> client = ParseInt(text.substr(0U, colon)); client && *client >= 1) {
            return { static_cast<std::size_t>(*client), text.substr(colon + 1U) };
        }
    }
    return { 1U, text };
}

struct Press {
    std::size_t client = 1U;
    kb::input::InputKey key = kb::input::InputKey::None;
    int first = 0;
    int count = 1;
};

struct Click {
    std::size_t client = 1U;
    UiClick click;
};

// One scene with the scripts of the project.
struct Peer {
    std::string label;
    kb::net::OpenedProjectScene opened;
    std::ostringstream log;
    std::unique_ptr<kb::script::ScriptRuntimeHost> host;
    std::unique_ptr<kb::net::SceneClient> client;       // a player
    std::unique_ptr<kb::net::ScriptNetworkRuntime> net;  // the server
    std::ostringstream network;
    bool connectedOnce = false;

    [[nodiscard]] kb::scene::Scene& Scene() { return *opened.scene; }
};

struct Report {
    std::vector<std::string> lines;
    std::vector<std::string> warnings;
    std::set<std::string> seen;
    std::size_t diagnostics = 0U;
    std::size_t dropped = 0U;

    void Line(std::string text) {
        if (lines.size() < kMaxLines) lines.push_back(std::move(text));
        else ++dropped;
    }
    void Warn(std::string text) {
        if (seen.insert(text).second) warnings.push_back(std::move(text));
    }
};

[[nodiscard]] bool OpenPeer(Peer& peer, const std::string& project, const std::string& scene, bool player, std::string& error) {
    if (!kb::net::OpenProjectScene(project, scene, peer.opened, error)) return false;
    kb::scene::PhysicsBackend::SetStepPipelining(peer.Scene(), false);
    if (!kb::scene::SceneDocumentService::LoadFileIntoScene(peer.Scene(), peer.opened.scenePath)) {
        error = "the scene could not be loaded: " + peer.opened.scenePath.generic_string();
        return false;
    }
    if (player) {
        kb::scene::SceneInputActivation::Apply(peer.Scene());
        const kb::project::ProjectSettingsLoadResult settings =
            kb::project::ProjectSettingsStore::Load(kb::project::ProjectSettingsStore::FilePath(peer.opened.projectRoot));
        if (settings.found && settings.Succeeded() && settings.settings.inputEnabled && !settings.settings.inputMappingContext.empty()) {
            const auto* mapping = peer.Scene().Assets().Manager().Registry().FindByPath(settings.settings.inputMappingContext);
            if (mapping == nullptr || mapping->type != "InputMappingContext" || !peer.Scene().Input().AddMappingContext(mapping->id.value, 0)) {
                error = "the project input mapping could not be activated: " + settings.settings.inputMappingContext;
                return false;
            }
        }
    }
    peer.host = std::make_unique<kb::script::ScriptRuntimeHost>(peer.Scene());
    if (!peer.host->Succeeded()) {
        error = "the script host failed: " + (peer.host->Diagnostics().empty() ? std::string{ "unknown error" } : peer.host->Diagnostics().front());
        return false;
    }
    RegisterScriptLog(*peer.host, peer.log, {});
    return true;
}

void MoveLines(Peer& peer, std::ostringstream& from, int frame, Report& report) {
    std::istringstream lines{ from.str() };
    from.str({});
    std::string line;
    while (std::getline(lines, line)) report.Line("frame " + std::to_string(frame) + " " + peer.label + ": " + line);
}

// What went wrong in the scripts of one scene during the last update, and the Net.* calls that failed.
void Collect(Peer& peer, int frame, Report& report) {
    kb::script::ScriptRuntimeHost& host = *peer.host;
    const auto add = [&](const std::string& what) {
        if (report.seen.insert(peer.label + what).second) {
            report.Line("frame " + std::to_string(frame) + " " + peer.label + ": " + what);
        }
        ++report.diagnostics;
    };
    if (const kb::script::ScriptRuntimeExecutionResult* result = host.InstalledSceneSystemLastResult()) {
        for (const kb::script::ScriptDiagnostic& diagnostic : result->diagnostics) {
            const std::string entity = std::string{ peer.Scene().Entities().Name(diagnostic.entity) };
            add("[script error] " + (entity.empty() ? std::string{} : "'" + entity + "', ") + diagnostic.message);
        }
    }
    if (const kb::script::ScriptRuntimeAssetPrepareResult* prepare = host.InstalledSceneSystemLastPrepareResult()) {
        for (const auto& diagnostic : prepare->diagnostics) add("[compile error] " + diagnostic.message);
    }
    for (const std::string& problem : peer.Scene().Runtime().DrainSceneSystemErrors()) add("[scene system error] " + problem);
    static_cast<void>(host.DrainSceneSystemDiagnostics());
    for (const std::string& problem : kb::script::ScriptNetApi::DrainProblems(peer.Scene())) report.Warn(peer.label + ": " + problem);
    MoveLines(peer, peer.log, frame, report);
    if (peer.client != nullptr) MoveLines(peer, peer.network, frame, report);
}

using Positions = std::map<kb::ecs::Entity::IdType, std::pair<std::string, kb::math::Vec3>>;

[[nodiscard]] Positions SnapshotPositions(Peer& peer) {
    Positions positions;
    struct Context {
        Peer* peer;
        Positions* positions;
    } context{ &peer, &positions };
    peer.Scene().Runtime().EcsWorld().ForEach<kb::scene::TransformComponent>(
        [](kb::ecs::Entity entity, const kb::scene::TransformComponent& transform, void* data) {
            auto* state = static_cast<Context*>(data);
            (*state->positions)[entity.Id()] = { std::string{ state->peer->Scene().Entities().Name(entity) }, transform.localPosition };
        },
        &context);
    return positions;
}

[[nodiscard]] std::string Text(const kb::math::Vec3& at) {
    std::ostringstream text;
    text.setf(std::ios::fixed);
    text.precision(2);
    text << at.x << ',' << at.y << ',' << at.z;
    return text.str();
}

[[nodiscard]] double PositionGap(const kb::math::Vec3& left, const kb::math::Vec3& right) {
    return std::sqrt(static_cast<double>((left.x - right.x) * (left.x - right.x) + (left.y - right.y) * (left.y - right.y) + (left.z - right.z) * (left.z - right.z)));
}

constexpr std::size_t kMaxTrace = 4000U;

// One line for the server and one for each player, for each replicated object called `name`: where it is now, and where the player's replica of it is.
void TraceReplicas(const std::string& name, int drawn, Peer& server, kb::net::ReplicationServer& replication, const std::vector<std::unique_ptr<Peer>>& players,
    std::vector<std::string>& lines) {
    for (const auto id : replication.NetworkIds()) {
        const kb::ecs::Entity entity = replication.EntityOf(id);
        if (!entity.IsValid()) continue;
        if (server.Scene().Entities().Name(entity) != name) continue;
        if (const auto* authoritative = server.Scene().Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(entity)) {
            lines.push_back("trace " + std::to_string(drawn) + " server " + name + "#" + std::to_string(id) + " " + Text(authoritative->localPosition));
        }
        for (const auto& player : players) {
            const kb::ecs::Entity replica = player->client->Replica(id);
            const auto* transform = replica.IsValid() ? player->Scene().Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(replica) : nullptr;
            if (transform != nullptr) lines.push_back("trace " + std::to_string(drawn) + " " + player->label + " " + name + "#" + std::to_string(id) + " " + Text(transform->localPosition));
        }
    }
}

constexpr std::string_view kUsage =
    "usage: kb_cli net check --project <dir> [--scene <path>] [--clients 2] [--frames 300] [--press [<client>:]<Key>@<frame>[+<frames>] ...]\n"
    "                        [--click [<client>:]<widget path or x,y>@<frame> ...] [--loss <percent>] [--latency <ms>]\n";

} // namespace

int RunNetCheck(const ArgumentList& arguments, CommandIo io) {
    static constexpr std::array<std::string_view, 10> kAllowed{ "--project", "--scene", "--clients", "--frames", "--press", "--click", "--loss", "--latency", "--trace", "--json" };
    if (const std::vector<std::string> unknown = arguments.UnknownOptions(kAllowed); !unknown.empty() || !arguments.Option("--project")) {
        io.err << (unknown.empty() ? "error: net check needs --project <dir>\n" : "error: net check has no option '" + unknown.front() + "'\n") << kUsage;
        return 1;
    }
    const auto number = [&](std::string_view name, int fallback, int low, int high, int& value) {
        value = fallback;
        if (const std::optional<std::string> text = arguments.Option(name)) {
            const std::optional<int> parsed = ParseInt(*text);
            if (!parsed || *parsed < low || *parsed > high) {
                io.err << "error: " << name << " expects a whole number from " << low << " to " << high << "\n";
                return false;
            }
            value = *parsed;
        }
        return true;
    };
    int clientCount = 2;
    int frames = 300;
    int loss = 0;
    int latency = 30;
    if (!number("--clients", 2, 1, static_cast<int>(kMaxClients), clientCount) || !number("--frames", 300, 10, 6000, frames) ||
        !number("--loss", 0, 0, 50, loss) || !number("--latency", 30, 0, 500, latency)) {
        return 1;
    }
    std::vector<Press> presses;
    for (const std::string& text : arguments.Options("--press")) {
        const Targeted target = SplitClient(text);
        const std::size_t at = target.rest.find('@');
        const std::size_t plus = target.rest.find('+', at == std::string_view::npos ? 0U : at);
        Press press;
        press.client = target.client;
        press.key = kb::input::ParseInputKey(target.rest.substr(0U, at));
        const std::optional<int> first = at == std::string_view::npos ? std::nullopt : ParseInt(target.rest.substr(at + 1U, plus == std::string_view::npos ? std::string_view::npos : plus - at - 1U));
        const std::optional<int> count = plus == std::string_view::npos ? std::optional<int>{ 1 } : ParseInt(target.rest.substr(plus + 1U));
        if (press.key == kb::input::InputKey::None || !first || !count || *first < 1 || *count < 1 || press.client > static_cast<std::size_t>(clientCount)) {
            io.err << "error: --press expects [<client>:]<Key>@<frame>[+<frames>] for one of the " << clientCount << " clients, e.g. --press 2:W@30+60; got '" << text << "'\n";
            return 1;
        }
        press.first = *first;
        press.count = *count;
        presses.push_back(press);
    }
    std::vector<Click> clicks;
    for (const std::string& text : arguments.Options("--click")) {
        const Targeted target = SplitClient(text);
        std::string error;
        std::optional<UiClick> click = ParseUiClick(target.rest, error);
        if (!click || target.client > static_cast<std::size_t>(clientCount)) {
            io.err << "error: " << (click ? "--click names a client that does not exist: " + text : error) << "\n";
            return 1;
        }
        clicks.push_back({ target.client, std::move(*click) });
    }
    const std::string project = *arguments.Option("--project");
    const std::string sceneOption = arguments.Option("--scene").value_or(std::string{});
    const std::string traceName = arguments.Option("--trace").value_or(std::string{});
    std::vector<std::string> trace;

    // The server.
    Report report;
    std::string error;
    Peer server;
    server.label = "server";
    if (!OpenPeer(server, project, sceneOption, false, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::net::LinkConditions conditions;
    conditions.simulation.latencyMilliseconds = static_cast<std::uint32_t>(latency);
    conditions.simulation.jitterMilliseconds = static_cast<std::uint32_t>(latency / 4);
    conditions.simulation.lossPermille = static_cast<std::uint16_t>(loss * 10);
    conditions.simulation.reorderPermille = static_cast<std::uint16_t>(loss != 0 ? 50 : 0);
    conditions.simulation.seed = 21U;
    kb::net::LoopbackNetwork network{ conditions };
    server.net = std::make_unique<kb::net::ScriptNetworkRuntime>(kb::net::ScriptNetworkRuntime::Role::Server, server.Scene(), server.opened.replication);
    if (!server.host->InstallSceneSystem()) {
        io.err << "error: the script scene system could not be installed\n";
        return 1;
    }
    std::unique_ptr<kb::net::ReplicationRegistry> components = kb::net::BuildReplicationRegistry(server.Scene().Runtime().EcsWorld(), server.opened.replication, 1000.0F, error);
    if (!components) { io.err << "error: " << error << '\n'; return 1; }
    kb::net::RpcRegistry rpcs;
    kb::net::DefineStandardRpcs(rpcs);
    if (!server.net->Freeze(rpcs, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::net::StaticKeyTable keys;
    for (int id = 1; id <= clientCount; ++id) keys.Set(static_cast<std::uint64_t>(id), KeyFor(static_cast<std::uint64_t>(id)));
    std::unique_ptr<kb::net::IDatagramTransport> transport = network.CreateTransport(kb::net::Endpoint::Loopback(kServerPort));
    kb::net::Host host{ *transport, kb::net::HostConfig{}, keys };
    if (!host.Valid()) {
        io.err << "error: the network host could not be created (random numbers unavailable?)\n";
        return 1;
    }
    kb::net::ReplicationServer replication{ host, server.Scene().Runtime().EcsWorld(), *components, rpcs };
    server.net->BindServer(replication);

    // The players.
    std::vector<std::unique_ptr<Peer>> players;
    std::uint16_t nextPort = kServerPort + 1U;
    for (int id = 1; id <= clientCount; ++id) {
        auto player = std::make_unique<Peer>();
        player->label = "client " + std::to_string(id);
        if (!OpenPeer(*player, project, sceneOption, true, error)) {
            io.err << "error: " << player->label << ": " << error << '\n';
            return 1;
        }
        kb::net::SceneClientOptions options;
        options.clientId = static_cast<std::uint64_t>(id);
        options.key = KeyFor(options.clientId);
        options.server = kb::net::Endpoint::Loopback(kServerPort);
        player->client = std::make_unique<kb::net::SceneClient>(player->Scene(), std::move(options), [&network, &nextPort](std::string& problem) {
            std::unique_ptr<kb::net::IDatagramTransport> next = network.CreateTransport(kb::net::Endpoint::Loopback(nextPort++));
            if (next == nullptr) problem = "no port left on the simulated network";
            return next;
        });
        if (!player->host->InstallSceneSystem()) {
            io.err << "error: " << player->label << ": the script scene system could not be installed\n";
            return 1;
        }
        players.push_back(std::move(player));
    }
    // A player that shows a widget needs a viewport to click on.
    if (!clicks.empty()) {
        for (auto& player : players) player->Scene().Input().MutableDeviceState().SetPointerViewportExtent(1280U, 720U);
    }

    // The run.
    Positions before;
    Positions earlier;
    std::map<kb::net::NetworkId, kb::math::Vec3> serverEarlier;
    std::uint32_t tick = 0U;
    for (int frame = 1; frame <= frames; ++frame) {
        const std::uint64_t now = static_cast<std::uint64_t>(frame) * 1000U / 30U;
        network.Advance(now);
        host.Update(now);
        kb::net::NetEvent event;
        while (host.PollEvent(event)) {
            server.net->HandleEvent(event);
            replication.HandleEvent(event, now);
        }
        for (auto& player : players) {
            if (!player->client->Update(now, player->network)) report.Warn(player->label + ": the session could not be updated");
            player->connectedOnce = player->connectedOnce || player->client->Connected();
        }
        for (const Press& press : presses) {
            kb::input::InputDeviceState& device = players[press.client - 1U]->Scene().Input().MutableDeviceState();
            if (frame == press.first) device.SetKeyDown(press.key, true);
            if (frame == press.first + press.count) device.SetKeyDown(press.key, false);
        }
        for (Click& entry : clicks) {
            Peer& player = *players[entry.client - 1U];
            kb::input::InputDeviceState& device = player.Scene().Input().MutableDeviceState();
            if (frame == entry.click.frame - 1) {
                std::string placeError;
                if (!PlaceUiClick(player.Scene(), entry.click, placeError)) {
                    io.err << "error: " << player.label << ": " << placeError << '\n';
                    return 1;
                }
                device.SetHasFocus(true);
                device.SetPointerPosition(entry.click.position.x, entry.click.position.y);
            }
            if (frame == entry.click.frame) device.SetKeyDown(kb::input::InputKey::MouseLeft, true);
            if (frame == entry.click.frame + 1) device.SetKeyDown(kb::input::InputKey::MouseLeft, false);
        }
        static_cast<void>(server.Scene().Runtime().Update(kStepSeconds));
        Collect(server, frame, report);
        replication.Tick(++tick, now);
        // Each player draws twice per step: the second time the server has said nothing new, so a replica that is not smoothed stands still.
        for (std::uint64_t half = 0U; half < 2U; ++half) {
            for (auto& player : players) {
                if (half == 1U && !player->client->Update(now + kHalfStepMilliseconds, player->network)) report.Warn(player->label + ": the session could not be updated");
                static_cast<void>(player->Scene().Runtime().Update(kStepSeconds / 2.0F));
                Collect(*player, frame, report);
            }
            if (!traceName.empty() && trace.size() < kMaxTrace) TraceReplicas(traceName, frame * 2 + static_cast<int>(half) - 1, server, replication, players, trace);
        }
        if (frame == 15) before = SnapshotPositions(server);
        if (frame == frames - 10) {
            earlier = SnapshotPositions(server);
            for (const auto id : replication.NetworkIds()) {
                const kb::ecs::Entity entity = replication.EntityOf(id);
                if (!entity.IsValid()) continue;
                if (const auto* transform = server.Scene().Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(entity)) serverEarlier[id] = transform->localPosition;
            }
        }
    }

    // What the run came to.
    const kb::net::ReplicationServerStats& stats = replication.Stats();
    std::size_t connected = 0U;
    for (const auto& player : players) connected += player->client->Connected() ? 1U : 0U;
    io.out << "net check: " << clientCount << " clients, " << frames << " frames of 1/30 s (the players draw 60 a second), link " << latency << " ms, " << loss << "% loss, scene "
           << server.opened.scenePath.filename().generic_string() << '\n';
    for (const std::string& line : report.lines) io.out << line << '\n';
    for (const std::string& line : trace) io.out << line << '\n';
    if (report.dropped != 0U) io.out << "... " << report.dropped << " more lines\n";
    io.out << "server: " << replication.ReadyClients() << " players, " << replication.ReplicatedCount() << " replicated objects, " << stats.rpcDispatched
           << " calls from players handled, " << (stats.rpcRejectedPermission + stats.rpcRejectedArguments + stats.rpcRejectedRate + stats.rpcRejectedDirection + stats.rpcRejectedMalformed)
           << " refused\n";
    for (auto& player : players) {
        io.out << player->label << ": " << (player->client->Connected() ? "connected" : "NOT connected") << ", " << player->client->ReplicaCount() << " replicas\n";
        if (!player->client->Connected() && player->client->DeclarationMismatches() == 0U) {
            report.Warn(player->label + " is not connected at the end" + (player->connectedOnce ? " (it was, and the session ended)" : " (it never connected)"));
        }
    }
    std::size_t replicatedIds = 0U;
    for (const auto id : replication.NetworkIds()) {
        const kb::ecs::Entity entity = replication.EntityOf(id);
        if (!entity.IsValid()) continue;
        ++replicatedIds;
        const auto* transform = server.Scene().Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(entity);
        if (transform == nullptr) continue;
        const std::string name{ server.Scene().Entities().Name(entity) };
        io.out << "replicated: " << (name.empty() ? "(unnamed)" : name) << " (net id " << id << ", owner " << replication.OwnerOf(id) << ") server " << Text(transform->localPosition);
        for (auto& player : players) {
            const kb::ecs::Entity replica = player->client->Replica(id);
            const auto* seen = replica.IsValid() ? player->Scene().Runtime().EcsWorld().TryGet<kb::scene::TransformComponent>(replica) : nullptr;
            io.out << " | " << player->label << " " << (seen != nullptr ? Text(seen->localPosition) : std::string{ "none" });
            const auto still = serverEarlier.find(id);
            if (player->client->Connected() && still != serverEarlier.end() && PositionGap(still->second, transform->localPosition) < 0.001) {
                if (seen == nullptr) {
                    report.Warn(player->label + " does not have '" + name + "', which the server replicates: the replication reached " + std::to_string(player->client->ReplicaCount()) +
                        " of " + std::to_string(replication.ReplicatedCount()) + " objects (a lossy link may need more frames)");
                } else if (PositionGap(seen->localPosition, transform->localPosition) > 0.25) {
                    report.Warn(player->label + " sees '" + name + "' at " + Text(seen->localPosition) + " but the server has it at " + Text(transform->localPosition) +
                        ": a change made on a client is not sent to the server - the client sends a call (Net.Call) and the server moves the object");
                }
            }
        }
        io.out << '\n';
    }
    if (replication.ReplicatedCount() == 0U) {
        report.Warn("nothing is replicated: the players see nothing the server changes. Mark an object in a server script with Net.Replicate(entity) (kb_cli recipe run net-object --entity <Name> does it), "
                    "or start the server with --replicate all");
    }
    // An object the server moved that the players cannot see.
    if (!before.empty()) {
        std::vector<std::string> moved;
        const Positions after = SnapshotPositions(server);
        for (const auto& [id, entry] : before) {
            const auto found = after.find(id);
            if (found == after.end() || replication.NetworkIdOf(kb::ecs::Entity{ id }) != 0U || entry.first.empty()) continue;
            if (PositionGap(entry.second, found->second.second) > 0.05) moved.push_back(entry.first);
        }
        std::ranges::sort(moved);
        if (!moved.empty()) {
            std::string names;
            for (std::size_t index = 0U; index < moved.size() && index < 3U; ++index) names += (index == 0U ? "'" : ", '") + moved[index] + "'";
            report.Warn("the server moved " + names + (moved.size() > 3U ? " and others" : "") + ", which " + (moved.size() == 1U ? "is" : "are") +
                " not replicated, so the players do not see it move. If they should, make it networked (kb_cli recipe run net-object --entity <Name>)");
        }
    }
    for (auto& player : players) {
        if (player->client->DeclarationMismatches() != 0U) {
            report.Warn(player->label + " left because its declarations differ from the server's: the server and every player must run the same scripts and declare "
                        "the same RPCs in Ready (do not wrap Net.RegisterRpc in `if Net.IsServer()`)");
        }
    }
    if (stats.rpcRejectedPermission != 0U) {
        report.Warn(std::to_string(stats.rpcRejectedPermission) + " calls were refused because the caller does not own the object: the server gives an object to a player with Net.SetOwner(entity, player) "
                    "(for example in OnNetConnected), and the player sends the replica it owns (the entity of OnNetSpawned whose owner is Net.LocalPlayerId())");
    }
    if (stats.rpcRejectedArguments != 0U || stats.rpcRejectedRate != 0U) {
        report.Warn(std::to_string(stats.rpcRejectedArguments) + " calls had arguments outside the declared ranges and " + std::to_string(stats.rpcRejectedRate) +
                    " came faster than maxPerSecond: widen the range or raise maxPerSecond in Net.RegisterRpc, or call less often");
    }
    for (const std::string& warning : report.warnings) io.out << "warning: " << warning << '\n';
    io.out << "done: " << report.diagnostics << " diagnostics, " << report.warnings.size() << " warnings\n";
    if (io.WantsData()) {
        kb::core::JsonValue data = DataObject("21kb.cli.net-check/v1");
        data.Set("clients", kb::core::JsonValue::MakeNumber(clientCount));
        data.Set("connected", kb::core::JsonValue::MakeNumber(static_cast<double>(connected)));
        data.Set("replicated", kb::core::JsonValue::MakeNumber(static_cast<double>(replication.ReplicatedCount())));
        data.Set("diagnostics", kb::core::JsonValue::MakeNumber(static_cast<double>(report.diagnostics)));
        auto warnings = kb::core::JsonValue::MakeArray();
        for (const std::string& warning : report.warnings) warnings.Append(kb::core::JsonValue::MakeString(ToValidUtf8(warning)));
        data.Set("warnings", std::move(warnings));
        PublishData(io, std::move(data));
    }
    return report.diagnostics == 0U && connected == players.size() ? 0 : 1;
}

} // namespace kb::cli

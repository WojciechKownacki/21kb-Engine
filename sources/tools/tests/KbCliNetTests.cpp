#include "CliCommands.hpp"
#include "CliProgram.hpp"
#include "CliRecipes.hpp"

#include "engine/script/ScriptAgentGuides.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptEventHooks.hpp"
#include "kb/net/Rpc.hpp"
#include "kb/net/SceneClient.hpp"
#include "kb/net/SceneReplication.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Multiplayer for an agent: `net info` is complete against the registries it reads, the customer's words find the recipes, `net setup` writes
// files the player's client accepts, `net check` tells the usual mistakes with what to do, and every networked recipe works on a server with
// two players: what one player does reaches the other.
namespace {

namespace fs = std::filesystem;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        std::fputs(message.c_str(), stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

struct Run {
    int exitCode = 0;
    std::string out;
    std::string err;
    [[nodiscard]] std::string All() const { return out + err; }
};

[[nodiscard]] Run Cli(const std::vector<std::string>& words) {
    std::istringstream in;
    std::ostringstream out;
    std::ostringstream err;
    const int exitCode = kb::cli::RunCli(words, in, kb::cli::CommandIo{ .out = out, .err = err });
    return Run{ exitCode, out.str(), err.str() };
}

[[nodiscard]] fs::path Root() {
    return fs::temp_directory_path() / "21kb_kb_cli_net_tests";
}

[[nodiscard]] std::string Project(std::string_view name) {
    kb::cli::ResetCliConfinementForTests();
    std::error_code error;
    fs::remove_all(Root() / std::string{ name }, error);
    fs::create_directories(Root(), error);
    const Run made = Cli({ "new", "--parent", Root().string(), "--name", std::string{ name }, "--template", "Basic" });
    Require(made.exitCode == 0, "the project could not be made: " + made.All());
    return (Root() / std::string{ name }).string();
}

void RunRecipe(const std::string& project, const std::vector<std::string>& words) {
    std::vector<std::string> command{ "recipe", "run" };
    command.insert(command.end(), words.begin(), words.end());
    command.insert(command.end(), { "--project", project });
    const Run made = Cli(command);
    Require(made.exitCode == 0, "the recipe " + words.front() + " must run: " + made.All());
}

// A Lua script of the project on a new empty object.
void AddScript(const std::string& project, const std::string& name, const std::string& code) {
    Require(Cli({ "entity", "add", "--project", project, "--name", name }).exitCode == 0, "an object for " + name);
    Require(Cli({ "script-new", "--project", project, "--name", name, "--language", "lua", "--code", code }).exitCode == 0, "the script " + name);
    Require(Cli({ "scene-attach", "--project", project, "--node", name, "--script", "/Game/Scripts/" + name + ".lua" }).exitCode == 0, "attach " + name);
}

[[nodiscard]] Run Check(const std::string& project, const std::vector<std::string>& more = {}) {
    std::vector<std::string> words{ "net", "check", "--project", project };
    words.insert(words.end(), more.begin(), more.end());
    return Cli(words);
}

[[nodiscard]] std::size_t Count(const std::string& text, std::string_view part) {
    std::size_t count = 0U;
    for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++count;
    return count;
}

// "replicated: Crate (net id 1, owner 0) server 0.00,5.00,0.00 | client 1 0.00,5.00,0.00 | client 2 none": the places on the server, then on each client.
[[nodiscard]] std::vector<std::vector<double>> ReplicatedAt(const std::string& out, std::string_view name, int owner = -1) {
    std::istringstream lines{ out };
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.starts_with("replicated: " + std::string{ name }) || (owner >= 0 && !Contains(line, "owner " + std::to_string(owner) + ")"))) continue;
        std::vector<std::vector<double>> places;
        std::size_t from = line.find(") server ");
        std::istringstream parts{ line.substr(from + 1U) };
        std::string part;
        while (std::getline(parts, part, '|')) {
            while (!part.empty() && part.back() == ' ') part.pop_back();
            const std::size_t space = part.find_last_of(' ');
            std::vector<double> place;
            std::istringstream numbers{ part.substr(space + 1U) };
            std::string number;
            while (std::getline(numbers, number, ',')) place.push_back(std::atof(number.c_str()));
            places.push_back(place.size() == 3U ? place : std::vector<double>{});
        }
        return places;
    }
    return {};
}

// Everywhere the same place, within what the replication quantises.
[[nodiscard]] bool Same(const std::vector<std::vector<double>>& places) {
    if (places.size() < 2U) return false;
    for (const std::vector<double>& place : places) {
        if (place.size() != 3U) return false;
        for (std::size_t axis = 0U; axis < 3U; ++axis) {
            if (std::abs(place[axis] - places.front()[axis]) > 0.1) return false;
        }
    }
    return true;
}

void RunInfoTests() {
    kb::script::ScriptApiCatalog catalog;
    std::string error;
    Require(kb::cli::BuildScriptApiCatalog(std::nullopt, catalog, error), "the script API catalog: " + error);
    const Run info = Cli({ "net", "info" });
    Require(info.exitCode == 0, "net info: " + info.All());
    // Every Net function the engine registers, every network event, every standard call and every replicated component is in it.
    std::size_t functions = 0U;
    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        if (!function.name.starts_with("Net.")) continue;
        ++functions;
        Require(Contains(info.out, "- " + function.name + "("), "net info must list " + function.name + ":\n" + info.out);
    }
    Require(functions >= 9U && Count(info.out, "\n- Net.") == functions, "net info lists the Net functions of the registry and no others: " + std::to_string(functions));
    for (const kb::script::ScriptEventHook& hook : kb::script::ScriptEventHooks::All()) {
        if (hook.group == "Network") Require(Contains(info.out, "- function " + std::string{ hook.name }), "net info must list the event " + std::string{ hook.name });
    }
    kb::net::RpcRegistry standard;
    kb::net::DefineStandardRpcs(standard);
    for (std::uint16_t id = 1U; id <= standard.Count(); ++id) Require(Contains(info.out, "- " + standard.Find(id)->name + "("), "net info must list the standard call " + standard.Find(id)->name);
    const std::unique_ptr<kb::net::ReplicationRegistry> components = kb::net::MakeSceneReplicationRegistry(1000.0F);
    for (std::size_t index = 0U; index < components->Count(); ++index) Require(Contains(info.out, "- " + components->At(index).Name() + ", "), "net info must list the component " + components->At(index).Name());
    Require(Contains(info.out, "offline") && Contains(info.out, "kb_dedicated_server") && Contains(info.out, "net check") && Contains(info.out, "net setup"), "net info says the modes and how to play: " + info.out);
    Require(Contains(Cli({ "help", "net" }).out, "check --project"), "help net must describe the command");
}

void RunSearchTests() {
    struct Wish {
        std::string_view words;
        std::string_view recipe;
    };
    const std::vector<Wish> wishes{
        { "gra dla 4 graczy przez internet", "recipe multiplayer-online" },
        { "multiplayer", "recipe multiplayer-" },
        { "kooperacja", "recipe multiplayer-" },
        { "a game for 2-4 players on a local network", "recipe multiplayer-lan" },
        { "gra dla 2-4 graczy w sieci lokalnej", "recipe multiplayer-lan" },
        { "play with friends over the internet with a host", "recipe multiplayer-online" },
        { "synchronizacja ruchu postaci", "recipe net-players" },
        { "players join a network game and each one moves a character", "recipe net-players" },
        { "wspolny licznik punktow", "recipe net-score" },
        { "a score shared by all players", "recipe net-score" },
        { "lobby z przyciskiem Start", "recipe net-lobby" },
        { "a lobby with a start button", "recipe net-lobby" },
        { "prosty czat", "recipe net-chat" },
        { "a simple chat between players", "recipe net-chat" },
        { "health of each player in a multiplayer game", "recipe net-health" },
        { "zdrowie graczy w grze sieciowej", "recipe net-health" },
        { "zrob ten obiekt sieciowym", "recipe net-object" },
        { "make this object networked so the players see it", "recipe net-object" },
    };
    for (const Wish& wish : wishes) {
        const Run found = Cli({ "guide", "--search", std::string{ wish.words } });
        Require(found.exitCode == 0, "guide --search: " + found.All());
        Require(Contains(found.out, wish.recipe) && std::ranges::count(found.out, '\n') <= 3, "'" + std::string{ wish.words } + "' must find " + std::string{ wish.recipe } + " among at most three answers, got: " + found.out);
        Require(Contains(found.out.substr(0U, found.out.find('\n')), "recipe ") || Contains(found.out.substr(0U, found.out.find('\n')), "guide multiplayer"),
            "'" + std::string{ wish.words } + "' must answer with a recipe first: " + found.out);
    }
    // Words about the player and the game alone are not a request for a network.
    for (const char* words : { "the player has health and can die", "a game with a character the player moves", "wrog ktory goni gracza", "player picks up items and sees an inventory" }) {
        const std::string found = Cli({ "guide", "--search", words }).out;
        Require(!Contains(found, "recipe net-") && !Contains(found, "recipe multiplayer-"), std::string{ "'" } + words + "' is not about a network: " + found);
    }
    // The best answer for an internet game with players is the one with a lobby.
    const std::string first = Cli({ "guide", "--search", "gra dla 4 graczy przez internet" }).out;
    Require(first.starts_with("recipe multiplayer-online"), "the internet game is the first answer: " + first);
    Require(Contains(*kb::script::ScriptAgentGuides::Topic("multiplayer"), "net check") && Contains(Cli({ "guide" }).out, "multiplayer"), "the guide multiplayer exists");
}

void RunSetupTests() {
    const std::string project = Project("NetSetup");
    const std::string out = (Root() / "NetSetupKeys").string();
    std::error_code ignored;
    fs::remove_all(out, ignored);
    const Run made = Cli({ "net", "setup", "--project", project, "--players", "3", "--address", "192.168.1.20", "--port", "27099", "--out", out });
    Require(made.exitCode == 0 && Contains(made.out, "kb_dedicated_server") && Contains(made.out, "kb_game"), "net setup: " + made.All());
    std::ifstream keys{ fs::path{ out } / "keys.txt" };
    std::string line;
    std::size_t count = 0U;
    while (std::getline(keys, line)) {
        if (line.empty() || line.front() == '#') continue;
        ++count;
        Require(line.size() == std::to_string(count).size() + 1U + 64U && line.starts_with(std::to_string(count) + " "), "a key line is '<id> <64 hex digits>': " + line);
    }
    Require(count == 3U, "one key for each player: " + std::to_string(count));
    // The session file is what the player's client reads, and it has the key the server has.
    for (int id = 1; id <= 3; ++id) {
        std::ifstream file{ fs::path{ out } / ("session-" + std::to_string(id) + ".json"), std::ios::binary };
        const std::string json{ std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} };
        kb::net::SceneClientOptions options;
        Require(kb::net::ParseNativeSceneClientSettings(json, options) && options.clientId == static_cast<std::uint64_t>(id) && options.server.port == 27099U,
            "the player's client must accept session-" + std::to_string(id) + ".json: " + json);
    }
    const Run again = Cli({ "net", "setup", "--project", project, "--players", "3", "--out", out });
    Require(again.exitCode == 1 && Contains(again.err, "--replace"), "keys are not overwritten by accident: " + again.All());
    Require(Cli({ "net", "setup", "--project", project, "--players", "3", "--out", out, "--replace" }).exitCode == 0, "--replace writes new keys");
    const Run inside = Cli({ "net", "setup", "--project", project, "--out", (fs::path{ project } / "Assets" / "Net").string() });
    Require(inside.exitCode == 1 && Contains(inside.err, "packs into the game"), "keys inside Assets are refused: " + inside.All());
    Require(Cli({ "net", "setup", "--project", project, "--address", "game.example.com", "--out", out }).exitCode == 1, "a name is not an IPv4 address");
    Require(Cli({ "net", "setup", "--project", project }).exitCode == 0 && fs::exists(fs::path{ project } / "Saved" / "Net" / "keys.txt"), "the default folder is Saved/Net");
}

// What the usual mistakes of a multiplayer script look like, and that each one says what to do.
void RunWarningTests() {
    {
        const std::string project = Project("NetNothing");
        const Run run = Check(project, { "--frames", "60" });
        Require(run.exitCode == 0 && Contains(run.out, "server: 2 players, 0 replicated objects") && Contains(run.out, "client 2: connected"), "two players connect to a project without scripts: " + run.All());
        Require(Contains(run.out, "warning: nothing is replicated") && Contains(run.out, "Net.Replicate") && Contains(run.out, "done: 0 diagnostics, 1 warnings"), "nothing replicated is a warning with the way out: " + run.out);
    }
    {
        // A crate that falls on the server and is not networked, then networked by the recipe.
        const std::string project = Project("NetCrate");
        Require(Cli({ "entity", "add", "--project", project, "--create", "Cube", "--name", "Crate", "--position", "0,5,0", "--body", "Dynamic" }).exitCode == 0, "a crate");
        const Run before = Check(project, { "--frames", "150" });
        Require(Contains(before.out, "warning: the server moved 'Crate'") && Contains(before.out, "net-object"), "an object that moves on the server and is not networked is named: " + before.out);
        RunRecipe(project, { "net-object", "--entity", "Crate" });
        const Run after = Check(project, { "--frames", "150" });
        Require(after.exitCode == 0 && !Contains(after.out, "warning:") && Contains(after.out, "done: 0 diagnostics, 0 warnings"), "the networked crate is clean: " + after.out);
        const std::vector<std::vector<double>> places = ReplicatedAt(after.out, "Crate");
        Require(places.size() == 3U && Same(places) && places[0][1] < 1.5, "the crate fell and both players see it where the server has it: " + after.out);
        Require(Contains(after.out, "client 1: [log] net-object Crate follows the server") && Contains(after.out, "client 2: [log] net-object Crate follows the server"),
            "the players' own crates follow the server's: " + after.out);
    }
    {
        // A player that tries what only the server may do.
        const std::string project = Project("NetClientWrites");
        AddScript(project, "Bad", "function Ready(self, dt)\n    CallFunction(\"Net.Replicate\", { entity = self.entity })\nend\n");
        const Run run = Check(project, { "--frames", "60" });
        Require(Contains(run.out, "warning: client 1: NotAuthority: Net.Replicate: only a server can replicate entities") && Contains(run.out, "Net.IsServer()"), "a player calling a server function is told how to guard it: " + run.out);
    }
    {
        // A call about an object the caller does not own.
        const std::string project = Project("NetNoOwner");
        AddScript(project, "Poke",
            "function Ready(self, dt)\n"
            "    CallFunction(\"Net.RegisterRpc\", { name = \"Poke\", direction = \"toServer\", args = \"entity\", owner = 0 })\n"
            "    if Net.IsServer() then CallFunction(\"Net.Replicate\", { entity = self.entity }) end\n"
            "end\n"
            "function OnNetSpawned(self, event) CallFunction(\"Net.Call\", { rpc = \"Poke\", e1 = event.args.entity }) end\n"
            "function OnRpc_Poke(self, event) Log(\"poked\") end\n");
        const Run run = Check(project, { "--frames", "90" });
        Require(Contains(run.out, "warning: 2 calls were refused because the caller does not own the object") && Contains(run.out, "Net.SetOwner") && !Contains(run.out, "poked"),
            "a call without an owner is refused and explained: " + run.out);
    }
    {
        // Scripts that differ between the server and the players.
        const std::string project = Project("NetDiffer");
        AddScript(project, "Differ", "function Ready(self, dt)\n    if Net.IsServer() then CallFunction(\"Net.RegisterRpc\", { name = \"OnlyServer\", direction = \"toClients\" }) end\nend\n");
        const Run run = Check(project, { "--frames", "90" });
        Require(Contains(run.out, "left because its declarations differ from the server's") && Contains(run.out, "Net.RegisterRpc"), "a player whose RPCs differ is told why: " + run.out);
    }
    {
        // The command itself: a wrong option and a wrong number are named.
        const std::string project = Project("NetArgs");
        Require(Check(project, { "--clients", "0" }).exitCode == 1 && Check(project, { "--press", "9:W@5" }).exitCode == 1 && Check(project, { "--nope", "1" }).exitCode == 1, "bad options are refused");
        Require(Cli({ "net", "check" }).exitCode == 1, "a project is needed");
    }
}

// Each recipe on a server with two players: what one does reaches the other.
void RunRecipeChecks() {
    {
        const std::string project = Project("NetPlayers");
        RunRecipe(project, { "net-players" });
        const Run run = Check(project, { "--frames", "180", "--press", "1:W@40+40" });
        Require(run.exitCode == 0 && Contains(run.out, "player 1 joined") && Contains(run.out, "player 2 joined") && Contains(run.out, "done: 0 diagnostics"), "two players join: " + run.out);
        const std::vector<std::vector<double>> first = ReplicatedAt(run.out, "NetAvatar", 1);
        const std::vector<std::vector<double>> second = ReplicatedAt(run.out, "NetAvatar", 2);
        Require(first.size() == 3U && first[0][2] > 1.0 && Same(first), "player 1 walked forward on the server and player 2 sees it: " + run.out);
        Require(second.size() == 3U && std::abs(second[0][2] + 3.0) < 0.5 && Same(second), "player 2 stood still and everyone knows: " + run.out);
        Require(!Contains(run.out, "warning:"), "no warnings: " + run.out);
        // A player that leaves takes its character with it.
        Require(Contains(Check(project, { "--frames", "60", "--clients", "1" }).out, "server: 1 players, 1 replicated objects"), "one player, one character");
    }
    {
        // State that is not a position goes as a call: health, damage, and a late look at it.
        const std::string project = Project("NetHealth");
        RunRecipe(project, { "net-players" });
        RunRecipe(project, { "net-health" });
        const Run run = Check(project, { "--frames", "180", "--press", "1:W@40+40" });
        Require(run.exitCode == 0 && Contains(run.out, "server: [log] health of NetAvatar is 90") && !Contains(run.out, "is 80") && !Contains(run.out, "warning:"), "the server lowers the health once: " + run.out);
        for (const char* label : { "client 1", "client 2" }) {
            Require(Contains(run.out, std::string{ label } + ": [log] player 1 health 90") && Contains(run.out, std::string{ label } + ": [log] player 2 health 100"),
                std::string{ label } + " sees the health of both characters, the damaged one too: " + run.out);
        }
    }
    {
        // The coin game on a local network, on a clean link and on a lossy one.
        for (const char* loss : { "0", "10" }) {
            const std::string project = Project(std::string{ "NetLan" } + loss);
            RunRecipe(project, { "multiplayer-lan" });
            const Run run = Check(project, { "--frames", "420", "--press", "1:W@40+40", "--loss", loss });
            Require(run.exitCode == 0 && Contains(run.out, "[log] collected Coin") && Contains(run.out, "client 1: [log] score is 1") && Contains(run.out, "client 2: [log] score is 1"),
                std::string{ "a coin one player takes is a point for both (loss " } + loss + "%): " + run.out);
            Require(!Contains(run.out, "warning:"), "no warnings: " + run.out);
        }
    }
    {
        // The game over the internet: the host (whoever joined first; only its button works) starts it from the lobby, a chat line reaches everyone.
        const std::string project = Project("NetOnline");
        RunRecipe(project, { "multiplayer-online" });
        Require(Cli({ "component", "set", "--project", project, "--entity", "ChatInput", "--type", "Text", "--set", "content=hello" }).exitCode == 0, "a line in the chat input");
        const Run run = Check(project, { "--frames", "300", "--click", "1:Canvas/LobbyStart@60", "--click", "2:Canvas/LobbyStart@62", "--press", "1:W@120+40", "--click", "2:Canvas/ChatSend@200" });
        Require(run.exitCode == 0 && Contains(run.out, "lobby: 2 joined") && Contains(run.out, "game started with 2 players") && Contains(run.out, "client 2: [log] the game began"), "the host starts the game: " + run.out);
        Require(Contains(run.out, "client 1: [log] chat player 2: hello") && Contains(run.out, "client 2: [log] chat player 2: hello"), "a chat line reaches every player: " + run.out);
        Require(Contains(run.out, "client 2: [log] score is 1") && !Contains(run.out, "warning:"), "the game runs after the lobby: " + run.out);
        // Before anyone presses Start there are no characters.
        const Run waiting = Check(project, { "--frames", "100" });
        Require(Contains(waiting.out, "0 replicated objects") || !Contains(waiting.out, "NetAvatar"), "no character before the start: " + waiting.out);
    }
}

// What a player sees of another player's character while it walks: the server says where it is 30 times a second and the player draws 60, so
// without smoothing the character stands still every second picture. `--trace` lists where each player's replica is at every picture.
void RunSmoothnessTest() {
    const std::string project = Project("NetSmooth");
    RunRecipe(project, { "net-players" });
    const Run run = Check(project, { "--frames", "150", "--press", "1:W@40+40", "--trace", "NetAvatar" });
    Require(run.exitCode == 0 && Contains(run.out, "trace "), "the trace lists the replicas: " + run.All());
    // Client 2's replicas by network id, in the order drawn: the z of each.
    std::map<std::string, std::vector<double>> walks;
    std::istringstream lines{ run.out };
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.starts_with("trace ")) continue;
        std::istringstream words{ line };
        std::string trace, drawn, who, number, name, place;
        words >> trace >> drawn >> who >> number;
        if (who != "client" || number != "2") continue;
        words >> name >> place;
        walks[name].push_back(std::atof(place.substr(place.find_last_of(',') + 1U).c_str()));
    }
    Require(!walks.empty(), "client 2 has replicas to trace: " + run.out);
    // The character that walked: from the start of the walk, the steps of 1/60 s while it goes from z = -2 to z = 2.5 at a steady speed.
    std::vector<double> steps;
    for (const auto& [name, zs] : walks) {
        if (zs.empty() || *std::ranges::max_element(zs) < 2.5) continue;
        for (std::size_t index = 1U; index < zs.size(); ++index) {
            if (zs[index - 1U] > -2.0 && zs[index] < 2.5) steps.push_back(zs[index] - zs[index - 1U]);
        }
    }
    Require(steps.size() >= 20U, "a walk of at least 20 pictures: " + std::to_string(steps.size()));
    double mean = 0.0;
    for (const double step : steps) mean += step / static_cast<double>(steps.size());
    std::string all;
    for (const double step : steps) all += std::to_string(step).substr(0U, 6U) + " ";
    for (const double step : steps) {
        Require(step > mean * 0.4 && step < mean * 2.5, "the walking character of another player moves smoothly: a step of " + std::to_string(step) + " beside a mean of " + std::to_string(mean) + ": " + all);
    }
}

// Every networked recipe also runs with nobody connected: the same project, single-player, without a diagnostic.
void RunOfflineChecks() {
    for (const char* id : { "multiplayer-lan", "multiplayer-online" }) {
        const std::string project = Project(std::string{ "NetOffline_" } + id);
        RunRecipe(project, { id });
        const Run ran = Cli({ "run", "--project", project, "--frames", "200", "--press", "W@5+30" });
        Require(ran.exitCode == 0 && Contains(ran.out, " 0 diagnostics"), std::string{ "the recipe " } + id + " must run single-player: " + ran.All());
    }
}

} // namespace

void RunNetTests() {
    RunInfoTests();
    RunSearchTests();
    RunSetupTests();
    RunWarningTests();
    RunRecipeChecks();
    RunSmoothnessTest();
    RunOfflineChecks();
}

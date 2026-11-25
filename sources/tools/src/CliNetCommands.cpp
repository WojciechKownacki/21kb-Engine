#include "CliCommands.hpp"
#include "CliProgram.hpp"

#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/script/ScriptApiCatalog.hpp"
#include "engine/script/ScriptEventHooks.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/Host.hpp"
#include "kb/net/ByteIO.hpp"
#include "kb/net/Replication.hpp"
#include "kb/net/Rpc.hpp"
#include "kb/net/SceneReplication.hpp"
#include "kb/net/ProjectScene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneRuntime.hpp"
#include "kb/net/Session.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

// net: the engine's multiplayer for a person who makes a game. `info` says what there is, from the registries the engine runs on (the Net.*
// script functions, the script events, the replicated components, the standard calls and the limits), so it cannot fall behind them;
// `setup` makes the keys and the session files that connect the players to a server; `check` (CliNetCheck.cpp) tries a project with a
// server and players in one process.
namespace kb::cli {
namespace {

namespace fs = std::filesystem;

constexpr std::string_view kUsage =
    "usage: kb_cli net info [--project <dir>]\n"
    "       kb_cli net setup --project <dir> [--players 4] [--address 127.0.0.1] [--port 27015] [--out <dir>] [--replace]\n"
    "       kb_cli net check --project <dir> [--scene <path>] [--clients 2] [--frames 300] [--press [<client>:]<Key>@<frame>[+<frames>] ...]\n"
    "                        [--click [<client>:]<widget path or x,y>@<frame> ...] [--loss <percent>] [--latency <ms>]\n";

[[nodiscard]] const char* ArgTypeName(kb::net::RpcArgType type) noexcept {
    switch (type) {
    case kb::net::RpcArgType::Bool: return "bool";
    case kb::net::RpcArgType::Int32: return "int";
    case kb::net::RpcArgType::UInt32: return "uint";
    case kb::net::RpcArgType::Float: return "float";
    case kb::net::RpcArgType::String: return "string";
    case kb::net::RpcArgType::NetworkId: return "entity";
    }
    return "?";
}

[[nodiscard]] std::string FirstSentence(const std::string& text) {
    const std::size_t end = text.find(". ");
    if (end == std::string::npos) return text;
    // A short second sentence ("Server only.") belongs to the first.
    const std::size_t next = text.find(". ", end + 2U);
    const std::size_t second = (next == std::string::npos ? text.size() : next + 1U) - (end + 2U);
    return second <= 24U ? text.substr(0U, next == std::string::npos ? text.size() : next + 1U) : text.substr(0U, end + 1U);
}

// The facts of the registries, as text: what `net info` prints.
[[nodiscard]] std::string BuildInfo(const kb::script::ScriptApiCatalog& catalog, const kb::net::ReplicationRegistry& components, bool declarationsPresent) {
    std::ostringstream out;
    out << "# Multiplayer\n\n"
        << "Modes (Net.Mode().mode): offline - the scene runs alone and every Net.* call that needs a session says so; server - the one machine that runs the game "
           "(scripts, physics) and decides, a dedicated program without a window (kb_dedicated_server); client - a player's copy of the same project, connected to a server.\n"
        << "There is no host-and-play mode in one window and no lobby service: the server is its own program and a player joins with its address, a client id and a key "
           "(`net setup` makes them). A lobby is a script (recipe net-lobby).\n\n";

    out << "## Script functions\n";
    for (const kb::script::ScriptApiCatalogFunction& function : catalog.functions) {
        if (!function.name.starts_with("Net.")) continue;
        std::string inputs;
        for (const kb::script::ScriptApiPin& pin : function.inputs) inputs += (inputs.empty() ? "" : ", ") + pin.name;
        out << "- " << function.name << '(' << inputs << ") - " << FirstSentence(function.description) << '\n';
    }

    out << "\n## Events a script answers\n";
    for (const kb::script::ScriptEventHook& hook : kb::script::ScriptEventHooks::All()) {
        if (hook.group != "Network") continue;
        std::string args;
        for (const kb::script::ScriptEventHookArgument& argument : hook.args) args += (args.empty() ? "" : ", ") + std::string{ argument.name };
        out << "- function " << hook.name << (hook.name == "OnRpc_" ? "<name>" : "") << hook.receives << " - " << hook.when << (args.empty() ? "" : "; event.args: " + args) << '\n';
    }

    out << "\n## What is synchronised\nThe server sends, for each object it marks (Net.Replicate), these components, and nothing else:\n";
    if (!declarationsPresent) out << "Config/Replication.json is absent: only transform groups are synchronised.\n";
    for (std::size_t index = 0U; index < components.Count(); ++index) {
        const kb::net::ComponentCodec& codec = components.At(index);
        out << "- " << codec.Name() << ", " << codec.EncodedSize() << " bytes, audience " << kb::network::ReplicationAudienceName(codec.Audience()) << '\n';
    }
    out << "Replicated fields and variables are declared in Config/Replication.json. Other messages use Net.RegisterRpc and Net.Call.\n"
        << "A player's copy of a replicated object is a replica without a mesh: a script reads its place with Transform.GetPosition(replica) and moves a visible object of its own to it.\n";

    out << "\n## Calls the dedicated server defines itself (a script cannot send them; declare its own)\n";
    kb::net::RpcRegistry standard;
    kb::net::DefineStandardRpcs(standard);
    for (std::uint16_t id = 1U; id <= standard.Count(); ++id) {
        const kb::net::RpcDefinition* definition = standard.Find(id);
        std::string args;
        for (const kb::net::RpcArgSpec& argument : definition->args) args += (args.empty() ? "" : ", ") + std::string{ ArgTypeName(argument.type) };
        out << "- " << definition->name << '(' << args << ") " << (definition->direction == kb::net::RpcDirection::ClientToServer ? "player to server" : "server to player")
            << (definition->reliable ? ", reliable" : ", unreliable") << '\n';
    }

    const kb::net::HostConfig host;
    out << "\n## Limits\n"
        << "- players: up to " << host.maxClients << " per host (kb_dedicated_server --max-clients, 16 by default)\n"
        << "- replicated objects: up to 4294967294 slots; components per object: " << kb::net::kMaxReplicatedComponents << '\n'
        << "- one call: at most " << kb::net::kMaxRpcArgs << " arguments and " << kb::net::kMaxRpcPayloadBytes << " bytes; Net.Call has 4 number, 2 string, 2 bool and 2 entity slots; at most 30 calls a second per call and player unless maxPerSecond says more\n"
        << "- a datagram: " << kb::net::kMaxDatagramBytes << " bytes; a silent player is dropped after " << host.session.timeoutMilliseconds / 1000U << " s\n";

    out << "\n## Play\n"
        << "1. `kb_cli net check --project <dir>` tries the project with a server and 2 players in one process, no window (`--clients 4`, `--press 2:W@30+60` holds W for player 2, `--loss 10` drops 10% of the datagrams).\n"
        << "2. `kb_cli net setup --project <dir> --players 4 --address <the server's address>` writes the keys and one session file per player under Saved/Net.\n"
        << "3. The server: `kb_dedicated_server --project <dir> --keys Saved/Net/keys.txt --replicate none` (the scripts mark what is replicated); a player: `kb_game --project <dir> --network-config=Saved/Net/session-1.json`.\n"
        << "Over the internet the server's UDP port must be reachable (a forwarded port or a machine with a public address): the engine has no relay.\n";
    return out.str();
}

[[nodiscard]] int Info(const ArgumentList& arguments, CommandIo io) {
    kb::script::ScriptApiCatalog catalog;
    std::string error;
    if (!BuildScriptApiCatalog(arguments.Option("--project"), catalog, error)) {
        io.err << "error: " << error << '\n';
        return 1;
    }
    kb::scene::Scene scene;
    const auto project = arguments.Option("--project").value_or("");
    if (!project.empty() && !scene.Assets().MountProject(project)) { io.err << "error: project assets could not be mounted\n"; return 1; }
    kb::network::ReplicationDeclarations declarations;
    if (!kb::net::LoadReplicationDeclarations(scene, declarations, error)) { io.err << "error: " << error << '\n'; return 1; }
    auto components = kb::net::BuildReplicationRegistry(scene.Runtime().EcsWorld(), declarations, 1000.0F, error);
    if (!components) { io.err << "error: " << error << '\n'; return 1; }
    const bool present = !project.empty() && std::filesystem::exists(std::filesystem::path(project) / "Config" / "Replication.json");
    const std::string text = BuildInfo(catalog, *components, present);
    io.out << text;
    if (io.WantsData()) {
        kb::core::JsonValue data = DataObject("21kb.cli.net-info/v1");
        data.Set("text", kb::core::JsonValue::MakeString(ToValidUtf8(text)));
        PublishData(io, std::move(data));
    }
    return 0;
}

[[nodiscard]] bool IsInside(const fs::path& path, const fs::path& folder) {
    std::error_code error;
    const fs::path relative = fs::weakly_canonical(path, error).lexically_relative(fs::weakly_canonical(folder, error));
    return !error && !relative.empty() && *relative.begin() != "..";
}

[[nodiscard]] int Setup(const ArgumentList& arguments, CommandIo io) {
    static constexpr std::array<std::string_view, 8> kAllowed{ "--project", "--players", "--address", "--port", "--out", "--replace", "--json", "--dry-run" };
    const std::vector<std::string> unknown = arguments.UnknownOptions(kAllowed);
    const std::optional<std::string> project = arguments.Option("--project");
    if (!unknown.empty() || !project.has_value()) {
        io.err << (unknown.empty() ? "error: net setup needs --project <dir>\n" : "error: net setup has no option '" + unknown.front() + "'\n") << kUsage;
        return 1;
    }
    const auto whole = [&](std::string_view name, int fallback, int low, int high, int& value) {
        value = fallback;
        if (const std::optional<std::string> text = arguments.Option(name)) {
            char* end = nullptr;
            const long parsed = std::strtol(text->c_str(), &end, 10);
            if (text->empty() || *end != '\0' || parsed < low || parsed > high) {
                io.err << "error: " << name << " expects a whole number from " << low << " to " << high << '\n';
                return false;
            }
            value = static_cast<int>(parsed);
        }
        return true;
    };
    int players = 4;
    int port = 27015;
    if (!whole("--players", 4, 1, 64, players) || !whole("--port", 27015, 1, 65535, port)) return 1;
    const std::string address = arguments.Option("--address").value_or("127.0.0.1");
    // A session file holds a literal IPv4 address (docs/native_network_client.md).
    {
        std::istringstream parts{ address };
        std::string part;
        int count = 0;
        bool valid = !address.empty();
        while (std::getline(parts, part, '.')) {
            char* end = nullptr;
            const long octet = std::strtol(part.c_str(), &end, 10);
            valid = valid && !part.empty() && *end == '\0' && octet >= 0 && octet <= 255;
            ++count;
        }
        if (!valid || count != 4 || address.back() == '.') {
            io.err << "error: --address must be an IPv4 address such as 192.168.1.20 (the session file has no room for a name); got '" << address << "'\n";
            return 1;
        }
    }
    const fs::path root = fs::absolute(fs::path{ *project });
    std::error_code ignored;
    if (!fs::is_directory(root, ignored)) {
        io.err << "error: the project directory was not found: " << *project << '\n';
        return 1;
    }
    const fs::path out = arguments.Option("--out") ? fs::absolute(fs::path{ *arguments.Option("--out") }) : root / "Saved" / "Net";
    if (IsInside(out, root / "Assets")) {
        io.err << "error: --out " << out.generic_string() << " is inside the project's Assets, which a build packs into the game: every player would get every key. "
                  "Use a folder outside it (the default is Saved/Net)\n";
        return 1;
    }
    const fs::path keysFile = out / "keys.txt";
    if (!arguments.Flag("--replace")) {
        for (const fs::path& file : { keysFile, out / "session-1.json" }) {
            if (fs::exists(file, ignored)) {
                io.err << "error: " << file.generic_string() << " exists; --replace writes new keys over it (the old session files stop working)\n";
                return 1;
            }
        }
    }
    if (arguments.Flag("--dry-run")) {
        io.out << "dry run: would write " << keysFile.generic_string() << " and " << players << " session files, nothing was written\n";
        return 0;
    }
    fs::create_directories(out, ignored);
    std::string keys = "# <client id> <key>: the players this server accepts. Keep this file and the session files private.\n";
    std::unordered_set<std::uint64_t> ids;
    for (int id = 1; id <= players; ++id) {
        std::uint64_t clientId = 0U;
        do {
            std::array<std::uint8_t, 8> bytes{};
            if (!kb::security::SecureRandom(bytes)) {
                io.err << "error: the system could not supply random bytes for an id\n";
                return 1;
            }
            kb::net::ByteReader reader(bytes);
            clientId = reader.U64();
        } while (clientId <= 1U || !ids.insert(clientId).second);
        std::array<std::uint8_t, 32> key{};
        if (!kb::security::SecureRandom(key)) {
            io.err << "error: the system could not supply random bytes for a key\n";
            return 1;
        }
        const std::string hex = kb::security::ToHex(key);
        keys += std::to_string(clientId) + ' ' + hex + '\n';
        std::ofstream session{ out / ("session-" + std::to_string(id) + ".json"), std::ios::binary | std::ios::trunc };
        session << "{\n  \"address\": \"" << address << "\",\n  \"port\": " << port << ",\n  \"clientId\": \"" << clientId << "\",\n  \"key\": \"" << hex
                << "\",\n  \"worldExtent\": 1000,\n  \"reconnectAttempts\": 3\n}\n";
        if (!session.good()) {
            io.err << "error: " << (out / ("session-" + std::to_string(id) + ".json")).generic_string() << " could not be written\n";
            return 1;
        }
    }
    std::ofstream keysOut{ keysFile, std::ios::binary | std::ios::trunc };
    keysOut << keys;
    if (!keysOut.good()) {
        io.err << "error: " << keysFile.generic_string() << " could not be written\n";
        return 1;
    }
#if !defined(_WIN32)
    fs::permissions(keysFile, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ignored);
    if (ignored) {
        io.err << "error: private key file permissions could not be set\n";
        return 1;
    }
#endif
    // The project now runs online: the editor reads this mode when it builds an Android package (its permissions).
    const fs::path settingsPath = kb::project::ProjectSettingsStore::FilePath(root);
    std::error_code settingsError;
    fs::create_directories(settingsPath.parent_path(), settingsError);
    kb::project::ProjectSettingsLoadResult projectSettings = kb::project::ProjectSettingsStore::Load(settingsPath);
    if (!projectSettings.Succeeded()) {
        io.err << "error: the project settings could not be read: " << projectSettings.error << '\n';
        return 1;
    }
    projectSettings.settings.networkingMode = kb::project::ProjectNetworkingMode::Online;
    std::string saveError;
    if (!kb::project::ProjectSettingsStore::Save(settingsPath, projectSettings.settings, saveError)) {
        io.err << "error: the project settings could not be saved: " << saveError << '\n';
        return 1;
    }
    io.out << "wrote " << keysFile.generic_string() << " and session-1.json .. session-" << players << ".json in " << out.generic_string() << '\n'
           << "server: kb_dedicated_server --project " << *project << " --keys " << keysFile.generic_string() << " --replicate none --port " << port << '\n'
           << "player N: kb_game --project " << *project << " --network-config=" << (out / "session-N.json").generic_string() << '\n'
           << "(the session files hold the keys: give each player only their own, and never put them in the project's Assets)\n";
    return 0;
}

} // namespace

int RunNetCommand(const ArgumentList& arguments, CommandIo io) {
    const std::vector<std::string>& positionals = arguments.Positionals();
    const std::string action = positionals.empty() ? std::string{} : positionals.front();
    if (action == "info") return Info(arguments, io);
    if (action == "setup") return Setup(arguments, io);
    if (action == "check") return RunNetCheck(arguments, io);
    io.err << kUsage;
    return 1;
}

} // namespace kb::cli

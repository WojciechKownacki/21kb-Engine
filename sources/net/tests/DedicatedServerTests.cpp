// Starts the real kb_dedicated_server process on localhost and talks to it over real UDP sockets:
// handshake, replication of scene entities, RPC, wrong keys, hostile datagrams, limits, a clean stop.

#include "DedicatedServer.hpp"
#include "engine/core/JsonValue.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/project/ProjectSettings.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneBehaviourComponents.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneTransforms.hpp"
#include "engine/security/Crypto.hpp"
#include "kb/net/Replication.hpp"
#include "kb/net/SceneReplication.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef KB_DEDICATED_SERVER_PATH
#error KB_DEDICATED_SERVER_PATH must name the kb_dedicated_server executable
#endif

namespace {

using namespace kb::net;
namespace fs = std::filesystem;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

void WriteFile(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

bool WaitFor(const std::function<bool()>& condition, int timeoutMilliseconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMilliseconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

// A child process with its output sent to files.
class Process {
public:
    Process() = default;
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    ~Process() {
        if (Running()) {
            Kill();
        }
        Close();
    }

    bool Start(const std::vector<std::string>& arguments, const fs::path& stdoutFile, const fs::path& stderrFile) {
#if defined(_WIN32)
        SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        const auto open = [&](const fs::path& path) {
            return CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        };
        HANDLE out = open(stdoutFile);
        HANDLE err = open(stderrFile);
        std::wstring command = L"\"" + fs::path(KB_DEDICATED_SERVER_PATH).wstring() + L"\"";
        for (const std::string& argument : arguments) {
            command += L" \"" + fs::path(argument).wstring() + L"\"";
        }
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = out;
        startup.hStdError = err;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION info{};
        const BOOL created = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
        CloseHandle(out);
        CloseHandle(err);
        if (!created) {
            return false;
        }
        CloseHandle(info.hThread);
        process_ = info.hProcess;
        return true;
#else
        const pid_t child = fork();
        if (child < 0) {
            return false;
        }
        if (child == 0) {
            const int out = open(stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            const int err = open(stderrFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            dup2(out, 1);
            dup2(err, 2);
            std::vector<char*> argv;
            std::string exe = KB_DEDICATED_SERVER_PATH;
            argv.push_back(exe.data());
            std::vector<std::string> copy = arguments;
            for (std::string& argument : copy) {
                argv.push_back(argument.data());
            }
            argv.push_back(nullptr);
            execv(exe.c_str(), argv.data());
            _exit(127);
        }
        pid_ = child;
        return true;
#endif
    }

    // True while the process has not exited.
    bool Running() {
#if defined(_WIN32)
        if (process_ == nullptr) {
            return false;
        }
        DWORD code = 0;
        if (GetExitCodeProcess(process_, &code) && code != STILL_ACTIVE) {
            exitCode_ = static_cast<int>(code);
            return false;
        }
        return true;
#else
        if (pid_ <= 0) {
            return false;
        }
        int status = 0;
        const pid_t result = waitpid(pid_, &status, WNOHANG);
        if (result == pid_) {
            exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
            pid_ = 0;
            return false;
        }
        return true;
#endif
    }

    // Waits for the exit and returns its code, or -1 on a timeout.
    int Wait(int timeoutMilliseconds) {
        return WaitFor([this] { return !Running(); }, timeoutMilliseconds) ? exitCode_ : -1;
    }

    void Kill() {
#if defined(_WIN32)
        if (process_ != nullptr) {
            TerminateProcess(process_, 99);
        }
#else
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            int status = 0;
            waitpid(pid_, &status, 0);
            pid_ = 0;
        }
#endif
    }

private:
    void Close() {
#if defined(_WIN32)
        if (process_ != nullptr) {
            CloseHandle(process_);
            process_ = nullptr;
        }
#endif
    }

#if defined(_WIN32)
    HANDLE process_ = nullptr;
#else
    pid_t pid_ = 0;
#endif
    int exitCode_ = -1;
};

SessionKey KeyFrom(std::uint8_t seed) {
    SessionKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(seed * 3U + index);
    }
    return key;
}

std::string KeysFileText() {
    return "# test keys\n7 " + kb::security::ToHex(KeyFrom(7U)) + "\n8 " + kb::security::ToHex(KeyFrom(8U)) + " # second player\n";
}

struct TestProject {
    fs::path root;
    fs::path keys;
};

// The server script: declares an RPC pair, replicates its own entity and answers Echo.
constexpr const char* kServerScript = R"lua(
function Ready(self, dt)
    CallFunction("Net.RegisterRpc", { name = "Echo", direction = "toServer", args = "string(32)" })
    CallFunction("Net.RegisterRpc", { name = "EchoReply", direction = "toClients", args = "string(32)" })
    CallFunction("Net.RegisterRpc", { name = "Welcome", direction = "toClients", args = "uint" })
    CallFunction("Net.Replicate", { entity = self.entity })
end

-- A player who connected is greeted by id as soon as the session lets the server call it.
local greet = {}
function OnNetConnected(self, event)
    greet[#greet + 1] = event.args.player
end

function Tick(self, dt)
    for index = #greet, 1, -1 do
        if CallFunction("Net.Call", { rpc = "Welcome", target = greet[index], n1 = greet[index] }) then
            table.remove(greet, index)
        end
    end
end

function OnRpc_Echo(self, event)
    CallFunction("Net.Call", { rpc = "EchoReply", target = event.args.sender, s1 = event.args.s1 })
end
)lua";

// Registers the same RPCs a client of that script needs.
void DefineScriptRpcs(RpcRegistry& registry) {
    std::string error;
    RpcArgSpec text;
    text.type = RpcArgType::String;
    text.maxLength = 32U;
    Require(registry.Define({ "Echo", RpcDirection::ClientToServer, true, { text }, -1, 30U }, error), error);
    Require(registry.Define({ "EchoReply", RpcDirection::ServerToClient, true, { text }, -1, 30U }, error), error);
    RpcArgSpec number;
    number.type = RpcArgType::UInt32;
    number.min = 0.0;
    number.max = 4294967295.0;
    Require(registry.Define({ "Welcome", RpcDirection::ServerToClient, true, { number }, -1, 30U }, error), error);
}

// A project with four entities: two the server replicates by name (they start with Net_), one a script
// marks, and one that is not replicated.
TestProject MakeProject(const std::string& name) {
    TestProject project;
    // A directory of its own for every run, so that two runs (or a run and a leftover) cannot meet.
    project.root = fs::temp_directory_path() / ("21kb_dedicated_server_test_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    fs::remove_all(project.root, error);
    fs::create_directories(project.root / "Assets" / "Scenes");
    kb::project::ProjectDescriptor descriptor;
    Require(kb::project::ProjectManager::SaveProject(project.root / "Project.21kbproject", descriptor), "the project could not be written");
    kb::project::ProjectSettings settings;
    settings.name = "DedicatedServerTest";
    settings.gameName = settings.name;
    settings.defaultMap = "/Game/Scenes/Main.21kbscene";
    std::string settingsError;
    Require(kb::project::ProjectSettingsStore::Save(kb::project::ProjectSettingsStore::FilePath(project.root), settings, settingsError), settingsError);

    WriteFile(project.root / "Assets" / "Logic" / "Server.lua", kServerScript);
    kb::scene::Scene scene;
    Require(scene.Assets().MountProject(project.root) && scene.Assets().Discover() == 1U, "the project assets could not be mounted");
    const kb::assets::AssetMetadata* scriptAsset = scene.Assets().Manager().Registry().FindByPath("/Game/Logic/Server.lua");
    Require(scriptAsset != nullptr, "the server script is not in the project");
    const auto place = [&scene](const char* objectName, float x, float y, float z) {
        const auto object = scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = objectName });
        kb::scene::TransformComponent transform = *scene.Transforms().TryGet(object.Entity());
        transform.localPosition = { x, y, z };
        scene.Transforms().Set(object.Entity(), transform);
        return object.Entity();
    };
    place("Net_A", 10.0F, 2.0F, -3.0F);
    place("Net_B", -40.0F, 0.0F, 5.5F);
    place("Local_C", 1.0F, 1.0F, 1.0F);
    const kb::scene::SceneEntity hero = place("Script_Hero", 5.0F, 6.0F, 7.0F);
    scene.Components().Behaviours().Set(hero, kb::scene::BehaviourComponent{ .behaviourAssetId = scriptAsset->id.value, .backend = kb::scene::BehaviourBackend::Lua, .enabled = true });
    Require(kb::scene::SceneDocumentService::Save(scene, project.root / "Assets" / "Scenes" / "Main.21kbscene", "Main"), "the scene could not be saved");
    project.keys = project.root / "keys.txt";
    WriteFile(project.keys, KeysFileText());
    return project;
}

struct Server {
    explicit Server(const TestProject& project, std::vector<std::string> extra = {}, const std::string& tag = "run") : base(project.root / tag) {
        fs::create_directories(base);
        port = base / "port.txt";
        stop = base / "stop.txt";
        status = base / "status.json";
        out = base / "out.txt";
        err = base / "err.txt";
        std::vector<std::string> arguments{ "--project", project.root.string(), "--keys", project.keys.string(), "--port", "0", "--bind", "127.0.0.1",
            "--replicate", "prefix:Net_", "--port-file", port.string(), "--stop-file", stop.string(), "--status-file", status.string(), "--json" };
        arguments.insert(arguments.end(), extra.begin(), extra.end());
        started = process.Start(arguments, out, err);
    }

    [[nodiscard]] std::uint16_t WaitForPort() {
        if (!WaitFor([this] { return fs::exists(port) && !ReadFile(port).empty(); }, 15'000)) {
            return 0U;
        }
        return static_cast<std::uint16_t>(std::stoul(ReadFile(port)));
    }

    [[nodiscard]] kb::core::JsonValue Status() const {
        kb::core::JsonValue value;
        std::string error;
        const std::string text = ReadFile(status);
        return kb::core::JsonValue::Parse(text, value, error) ? value : kb::core::JsonValue{};
    }

    void RequestStop() const { WriteFile(stop, "stop"); }

    fs::path base, port, stop, status, out, err;
    Process process;
    bool started = false;
};

double Number(const kb::core::JsonValue& root, const char* a, const char* b = nullptr) {
    const kb::core::JsonValue* first = root.Find(a);
    if (first == nullptr) {
        return -1.0;
    }
    if (b == nullptr) {
        return first->AsNumber(-1.0);
    }
    const kb::core::JsonValue* second = first->Find(b);
    return second == nullptr ? -1.0 : second->AsNumber(-1.0);
}

// A client of the server: its own UDP socket, session and replica world.
struct RemoteClient {
    RemoteClient(std::uint16_t serverPort, std::uint64_t id, std::uint8_t keySeed, const kb::net::ClientConfig& config = {}) : started(std::chrono::steady_clock::now()) {
        std::string error;
        transport = CreateUdpTransport(Endpoint::Loopback(0U), error);
        Require(transport != nullptr, "the client socket could not be opened: " + error);
        components = MakeSceneReplicationRegistry(1000.0F);
        rpcs = std::make_unique<RpcRegistry>();
        DefineStandardRpcs(*rpcs);
        DefineScriptRpcs(*rpcs);
        world = std::make_unique<kb::ecs::World>();
        client = std::make_unique<Client>(*transport, config, id, KeyFrom(keySeed), Endpoint::Loopback(serverPort), Now());
        replication = std::make_unique<ReplicationClient>(*client, *world, *components, *rpcs);
        replication->SetRpcHandler("Pong", [this](std::span<const RpcValue> args) { pongs.push_back(std::get<std::uint32_t>(args[0])); });
        replication->SetRpcHandler("Welcome", [this](std::span<const RpcValue> args) { welcomes.push_back(std::get<std::uint32_t>(args[0])); });
        replication->SetRpcHandler("EchoReply", [this](std::span<const RpcValue> args) { echoes.push_back(std::get<std::string>(args[0])); });
        replication->SetRpcHandler("Said", [this](std::span<const RpcValue> args) {
            said.emplace_back(std::get<std::uint32_t>(args[0]), std::get<std::string>(args[1]));
        });
    }

    [[nodiscard]] std::uint64_t Now() const {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    }

    void Pump() {
        if (client != nullptr) {
            client->Update(Now());
            NetEvent event;
            while (client->PollEvent(event)) {
                if (event.type == NetEventType::Disconnected) {
                    lastReason = event.reason;
                    disconnected = true;
                }
                replication->HandleEvent(event);
            }
            replication->Flush();
        }
    }

    bool Until(const std::function<bool()>& condition, int timeoutMilliseconds) {
        return WaitFor([&] {
            Pump();
            return condition();
        }, timeoutMilliseconds);
    }

    std::chrono::steady_clock::time_point started;
    std::unique_ptr<IDatagramTransport> transport;
    std::unique_ptr<ReplicationRegistry> components;
    std::unique_ptr<RpcRegistry> rpcs;
    std::unique_ptr<kb::ecs::World> world;
    std::unique_ptr<Client> client;
    std::unique_ptr<ReplicationClient> replication;
    std::vector<std::uint32_t> pongs;
    std::vector<std::string> echoes;
    std::vector<std::uint32_t> welcomes;
    std::vector<std::pair<std::uint32_t, std::string>> said;
    DisconnectReason lastReason = DisconnectReason::None;
    bool disconnected = false;
};

bool HasReplica(RemoteClient& client, float x, float y, float z) {
    for (const NetworkId id : client.replication->ReplicaIds()) {
        const kb::scene::TransformComponent* transform = client.world->TryGet<kb::scene::TransformComponent>(client.replication->Find(id));
        if (transform != nullptr && std::fabs(transform->localPosition.x - x) < 0.05F && std::fabs(transform->localPosition.y - y) < 0.05F &&
            std::fabs(transform->localPosition.z - z) < 0.05F) {
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------ tests

void TestMainRun() {
    const TestProject project = MakeProject("main");
    Server server(project, { "--idle-timeout", "2" });
    Require(server.started, "the server process could not be started");
    const std::uint16_t port = server.WaitForPort();
    Require(port != 0U, "the server did not report a port; log: " + ReadFile(server.err));

    // Handshake and the state of the scene.
    RemoteClient first(port, 7U, 7U);
    Require(first.Until([&] { return first.client->State() == ClientState::Connected && first.replication->ReplicaCount() == 3U; }, 20'000),
        "the first client did not connect and receive the scene; log: " + ReadFile(server.err));
    Require(HasReplica(first, 10.0F, 2.0F, -3.0F) && HasReplica(first, -40.0F, 0.0F, 5.5F), "the replicated entities do not match the scene");
    Require(HasReplica(first, 5.0F, 6.0F, 7.0F), "the entity the server script marked with Net.Replicate was not replicated");
    Require(first.replication->ReplicaCount() == 3U, "an entity outside the replicated set was sent");

    // Another player, then RPC in both directions.
    RemoteClient second(port, 8U, 8U);
    Require(second.Until([&] { return second.replication->ReplicaCount() == 3U; }, 20'000), "the second client did not receive the scene");
    Require(first.replication->CallRpc("Ping", std::vector<RpcValue>{ std::uint32_t{ 4242U } }) == RpcStatus::Ok, "ping could not be sent");
    Require(first.Until([&] { return !first.pongs.empty(); }, 5000) && first.pongs.front() == 4242U, "the server did not answer the ping");
    Require(first.replication->CallRpc("Say", std::vector<RpcValue>{ std::string("hello from seven") }) == RpcStatus::Ok, "say could not be sent");
    const auto sawSaid = [](RemoteClient& client) { return !client.said.empty() && client.said.front().first == 7U && client.said.front().second == "hello from seven"; };
    Require(WaitFor([&] { first.Pump(); second.Pump(); return sawSaid(first) && sawSaid(second); }, 5000), "the broadcast did not reach both clients");
    // The server script greeted each player by id when it connected.
    Require(first.Until([&] { return !first.welcomes.empty(); }, 5000) && first.welcomes.front() == 7U, "the server script did not greet player 7");
    Require(second.Until([&] { return !second.welcomes.empty(); }, 5000) && second.welcomes.front() == 8U, "the server script did not greet player 8");
    // An RPC the server script declared and answers from Lua.
    Require(first.replication->CallRpc("Echo", std::vector<RpcValue>{ std::string("abc") }) == RpcStatus::Ok, "echo could not be sent");
    Require(first.Until([&] { return !first.echoes.empty(); }, 5000) && first.echoes.front() == "abc", "the server script did not answer the echo");
    // A call outside the declared range never reaches the server.
    Require(first.replication->CallRpc("Say", std::vector<RpcValue>{ std::string(300U, 'x') }) == RpcStatus::BadArguments, "a long chat line was accepted");

    // A wrong key and an unknown client never connect.
    ClientConfig shortWait;
    shortWait.connectTimeoutMilliseconds = 2000U;
    RemoteClient wrongKey(port, 7U, 99U, shortWait);
    RemoteClient unknown(port, 55U, 7U, shortWait);
    // The connected clients keep talking meanwhile, or the 2 second idle timeout would drop them.
    Require(unknown.Until([&] { first.Pump(); second.Pump(); wrongKey.Pump(); return wrongKey.disconnected && unknown.disconnected; }, 8000), "the clients that must not connect did not give up");
    Require(wrongKey.lastReason == DisconnectReason::ConnectTimeout && unknown.lastReason == DisconnectReason::ConnectTimeout, "a client that must not connect did not time out");

    // Hostile datagrams from a raw socket: random bytes, the handshake with the wrong sizes, nothing crashes.
    std::string socketError;
    auto raw = CreateUdpTransport(Endpoint::Loopback(0U), socketError);
    Require(raw != nullptr, "the raw socket could not be opened: " + socketError);
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (int packet = 0; packet < 600; ++packet) {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>((state >> 8U) % 1300U));
        for (std::uint8_t& byte : bytes) {
            state ^= state << 13U; state ^= state >> 7U; state ^= state << 17U;
            byte = static_cast<std::uint8_t>(state & 0xFFU);
        }
        if (bytes.size() >= 4U && packet % 2 == 0) {
            bytes[0] = 0x4EU; bytes[1] = 0x4BU; bytes[2] = 1U; bytes[3] = static_cast<std::uint8_t>(1U + packet % 3 * 0x0DU);
        }
        static_cast<void>(raw->Send(Endpoint::Loopback(port), bytes));
        if (packet % 50 == 0) {
            first.Pump();
        }
    }
    Require(WaitFor([&] { first.Pump(); second.Pump(); return Number(server.Status(), "host", "malformed") > 0.0 || Number(server.Status(), "host", "handshakes_rejected") > 0.0; }, 5000),
        "the server did not count the hostile datagrams");
    Require(server.process.Running(), "the server died under hostile datagrams");
    // Ping is unreliable and a flood can overflow the socket buffer, so it is asked again until it is answered.
    bool answered = false;
    for (int attempt = 0; attempt < 20 && !answered; ++attempt) {
        Require(first.replication->CallRpc("Ping", std::vector<RpcValue>{ std::uint32_t{ 7U } }) == RpcStatus::Ok, "ping could not be sent");
        answered = first.Until([&] { second.Pump(); return first.pongs.size() >= 2U; }, 400);
    }
    Require(answered && first.client->State() == ClientState::Connected, "the server stopped answering after the hostile datagrams");

    // Client 8 goes silent without saying goodbye: the idle timeout drops it.
    second.client.reset();
    second.replication.reset();
    Require(WaitFor([&] { first.Pump(); return Number(server.Status(), "clients_now") == 1.0; }, 12'000), "the idle timeout did not drop the silent client");

    // A clean stop: the exit code is 0, the clients are told, the summary says what happened.
    server.RequestStop();
    Require(server.process.Wait(20'000) == 0, "the server did not exit cleanly after the stop request; log: " + ReadFile(server.err));
    Require(first.Until([&] { return first.disconnected; }, 3000) && first.lastReason == DisconnectReason::PeerClosed, "the client was not told that the server stopped");
    kb::core::JsonValue summary;
    std::string error;
    Require(kb::core::JsonValue::Parse(ReadFile(server.out), summary, error), "the final status was not clean JSON: " + error + ": " + ReadFile(server.out));
    Require(summary.Find("schema") && summary.Find("schema")->AsString() == "21kb.dedicated-server/v1" && summary.Find("exit_reason")->AsString() == "stop-file",
        "the final status has the wrong schema or exit reason");
    Require(Number(summary, "clients_peak") >= 2.0 && Number(summary, "host", "handshakes_completed") >= 2.0 && Number(summary, "replicated_entities") == 3.0 &&
            Number(summary, "replication", "rpc_dispatched") >= 4.0 && Number(summary, "ticks") > 10.0 && Number(summary, "host", "handshakes_rejected") >= 1.0,
        "the final status does not match what happened: " + ReadFile(server.out));
    Require(ReadFile(server.err).find("listening on 127.0.0.1:" + std::to_string(port)) != std::string::npos && ReadFile(server.err).find("client 7 connected") != std::string::npos,
        "the log has no listening and connection lines");
    std::printf("  dedicated server: %.0f ticks, peak %.0f clients, %.0f spawns, %.0f state messages, %.0f RPC calls handled\n", Number(summary, "ticks"),
        Number(summary, "clients_peak"), Number(summary, "replication", "spawns_sent"), Number(summary, "replication", "state_messages_sent"),
        Number(summary, "replication", "rpc_dispatched"));
}

void TestLifetimeOptions() {
    const TestProject project = MakeProject("lifetime");
    // --max-seconds ends the run by itself.
    {
        Server server(project, { "--max-seconds", "1" }, "max");
        Require(server.started && server.process.Wait(15'000) == 0, "--max-seconds did not end the server cleanly");
        kb::core::JsonValue summary;
        std::string error;
        Require(kb::core::JsonValue::Parse(ReadFile(server.out), summary, error) && summary.Find("exit_reason")->AsString() == "max-seconds", "the exit reason is not max-seconds: " + ReadFile(server.out) + " log: " + ReadFile(server.err));
    }
    // --empty-timeout ends it once the last client has left.
    {
        Server server(project, { "--empty-timeout", "1" }, "empty");
        const std::uint16_t port = server.WaitForPort();
        Require(port != 0U, "no port");
        {
            RemoteClient client(port, 7U, 7U);
            Require(client.Until([&] { return client.client->State() == ClientState::Connected; }, 20'000), "the client did not connect");
            client.client->Disconnect();
            client.Until([&] { return client.disconnected; }, 3000);
        }
        Require(server.process.Wait(15'000) == 0, "--empty-timeout did not end the server");
        kb::core::JsonValue summary;
        std::string error;
        Require(kb::core::JsonValue::Parse(ReadFile(server.out), summary, error) && summary.Find("exit_reason")->AsString() == "empty-timeout", "the exit reason is not empty-timeout: " + ReadFile(server.out) + " log: " + ReadFile(server.err));
    }
    // The client limit: a second client over the limit never gets in.
    {
        Server server(project, { "--max-clients", "1" }, "limit");
        const std::uint16_t port = server.WaitForPort();
        Require(port != 0U, "no port");
        RemoteClient one(port, 7U, 7U);
        Require(one.Until([&] { return one.client->State() == ClientState::Connected; }, 20'000), "the first client did not connect");
        ClientConfig shortWait;
        shortWait.connectTimeoutMilliseconds = 2000U;
        RemoteClient two(port, 8U, 8U, shortWait);
        Require(two.Until([&] { return two.disconnected; }, 6000) && two.lastReason == DisconnectReason::ConnectTimeout, "a client over the limit got in");
        server.RequestStop();
        Require(server.process.Wait(20'000) == 0, "the server did not stop");
    }
}

void TestSceneCheckpoint() {
    const TestProject project = MakeProject("checkpoint");
    const fs::path checkpoint = project.root / "State.21kbscene";
    {
        Server server(project, { "--state-file", checkpoint.string() }, "first");
        Require(server.WaitForPort() != 0U, "checkpoint server did not start");
        server.RequestStop();
        Require(server.process.Wait(20'000) == 0, "checkpoint server did not stop cleanly");
        Require(server.Status().Find("state_saved") != nullptr && server.Status().Find("state_saved")->AsBool(), "checkpoint save was not reported");
    }
    auto saved = kb::scene::SceneDocumentService::Load(checkpoint);
    Require(saved.succeeded && saved.document.worldPrefab.NodeCount() >= 4U, "checkpoint did not preserve the scene");
    bool edited = false;
    for (std::uint32_t index = 0U; index < saved.document.worldPrefab.NodeCount(); ++index) {
        auto* node = saved.document.worldPrefab.TryGetMutableNode(index);
        if (node->name == "Net_A") {
            node->transform.localPosition.x = 123.0F;
            edited = true;
        }
    }
    Require(edited && kb::scene::SceneDocumentService::Save(saved.document, checkpoint), "could not prepare changed checkpoint state");
    {
        Server server(project, { "--state-file", checkpoint.string() }, "resume");
        Require(server.WaitForPort() != 0U, "checkpoint server did not resume");
        server.RequestStop();
        Require(server.process.Wait(20'000) == 0, "resumed server did not stop cleanly");
        Require(server.Status().Find("state_loaded") != nullptr && server.Status().Find("state_loaded")->AsBool(), "checkpoint resume was not reported");
    }
    const auto resumed = kb::scene::SceneDocumentService::Load(checkpoint);
    Require(resumed.succeeded, "resumed checkpoint is unreadable");
    bool preserved = false;
    for (const auto& node : resumed.document.worldPrefab.Nodes())
        if (node.name == "Net_A") preserved = node.transform.localPosition.x == 123.0F;
    Require(preserved, "server replaced checkpoint state with the original project scene");
    WriteFile(checkpoint, "corrupt checkpoint");
    {
        Server server(project, { "--state-file", checkpoint.string() }, "corrupt");
        Require(server.process.Wait(20'000) == 1, "corrupt checkpoint was silently discarded");
        Require(ReadFile(checkpoint) == "corrupt checkpoint", "failed startup overwrote the checkpoint");
    }
    const fs::path failedSave = project.root / "Failed.21kbscene";
    {
        Server server(project, { "--state-file", failedSave.string() }, "save-failure");
        Require(server.WaitForPort() != 0U, "save failure server did not start");
        fs::create_directory(failedSave);
        server.RequestStop();
        Require(server.process.Wait(20'000) == 1, "checkpoint write failure was reported as a clean stop");
        Require(ReadFile(server.out).find("state-save-error") != std::string::npos, "checkpoint failure has no final diagnostic");
    }
}

void TestCommandLine() {
    const TestProject project = MakeProject("cli");
    const fs::path out = project.root / "out.txt";
    const fs::path err = project.root / "err.txt";
    const auto run = [&](const std::vector<std::string>& arguments) {
        Process process;
        Require(process.Start(arguments, out, err), "the process could not be started");
        return process.Wait(15'000);
    };
    Require(run({ "--help" }) == 0 && ReadFile(out).find("--project") != std::string::npos && ReadFile(out).find("--stop-file") != std::string::npos, "--help is wrong");
    Require(run({ "--bogus", "1" }) == 2 && ReadFile(err).find("unknown option") != std::string::npos, "an unknown option was accepted");
    Require(run({ "--project", project.root.string() }) == 2, "missing --keys was accepted");
    Require(run({ "--project", project.root.string(), "--keys", project.keys.string(), "--port", "70000" }) == 2, "a port above 65535 was accepted");
    Require(run({ "--project", project.root.string(), "--keys", project.keys.string(), "--replicate", "everything" }) == 2, "a bad replicate mode was accepted");
    Require(run({ "--project", (project.root / "missing").string(), "--keys", project.keys.string() }) == 1 && ReadFile(err).find("project directory was not found") != std::string::npos,
        "a missing project did not fail with exit code 1");
    WriteFile(project.root / "bad_keys.txt", "7 nothex\n");
    Require(run({ "--project", project.root.string(), "--keys", (project.root / "bad_keys.txt").string() }) == 1 && ReadFile(err).find("keys file line 1") != std::string::npos,
        "a bad keys file did not fail with exit code 1");
    Require(run({ "--project", project.root.string(), "--keys", project.keys.string(), "--scene", "Assets/Scenes/Nope.21kbscene" }) == 1, "a missing scene did not fail");
    // The port is taken: the second server fails to start and says why.
    Server first(project, {}, "taken");
    const std::uint16_t port = first.WaitForPort();
    Require(port != 0U, "no port");
    Require(run({ "--project", project.root.string(), "--keys", project.keys.string(), "--bind", "127.0.0.1", "--port", std::to_string(port) }) == 1 &&
            ReadFile(err).find("UDP socket could not be opened") != std::string::npos,
        "a second server on the same port did not fail");
    first.RequestStop();
    Require(first.process.Wait(20'000) == 0, "the first server did not stop");
}

void TestKeysFile() {
    StaticKeyTable keys;
    std::size_t count = 0U;
    std::string error;
    Require(server::ParseKeysFile(KeysFileText(), keys, count, error) && count == 2U, "the keys file was not read: " + error);
    SessionKey key{};
    Require(keys.LookupKey(7U, key) && key == KeyFrom(7U) && keys.LookupKey(8U, key) && !keys.LookupKey(9U, key), "the keys are wrong");
    for (const char* bad : { "x y\n", "0 " "00" "\n", "5\n", "5 abc\n", "5 00 extra\n" }) {
        StaticKeyTable table;
        Require(!server::ParseKeysFile(bad, table, count, error), std::string("a bad keys file was accepted: ") + bad);
    }
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        { "keys-file", &TestKeysFile },
        { "scene-checkpoint", &TestSceneCheckpoint },
        { "command-line", &TestCommandLine },
        { "main-run", &TestMainRun },
        { "lifetime-options", &TestLifetimeOptions },
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

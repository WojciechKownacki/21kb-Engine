#include "CliCommands.hpp"
#include "MiniJson.hpp"

#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObjectDesc.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fputs(message, stderr);
        std::fputs("\n", stderr);
        std::exit(EXIT_FAILURE);
    }
}

[[nodiscard]] std::filesystem::path TestRoot() {
    return std::filesystem::temp_directory_path() / "21kb_engine_kb_cli_tests";
}

void ResetTestRoot() {
    std::error_code error;
    std::filesystem::remove_all(TestRoot(), error);
    std::filesystem::create_directories(TestRoot(), error);
    Require(!error, "kb_cli test root could not be prepared");
}

void WriteTextFile(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    Require(!error, "kb_cli test directory could not be created");
    std::ofstream output{ path, std::ios::binary | std::ios::trunc };
    Require(output.is_open(), "kb_cli test file could not be opened");
    output << text;
    Require(output.good(), "kb_cli test file could not be written");
}

[[nodiscard]] bool Contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

constexpr std::array<std::string_view, 2> kFlagNames{ "--disabled", "--quiet" };

struct CommandRun {
    int exitCode = 0;
    std::string output;
};

[[nodiscard]] CommandRun Run(int (*command)(const kb::cli::ArgumentList&, kb::cli::CommandIo), std::vector<std::string> arguments) {
    std::ostringstream captured;
    const kb::cli::ArgumentList parsed{ arguments, kFlagNames };
    const int exitCode = command(parsed, kb::cli::CommandIo{ .out = captured, .err = captured });
    return CommandRun{ .exitCode = exitCode, .output = captured.str() };
}

void RunMiniJsonTests() {
    kb::cli::JsonValue value;
    std::string error;
    Require(
        kb::cli::JsonValue::Parse(R"({"a": [1, -2.5, "x\nžż"], "b": {"c": true, "d": null}})", value, error),
        "MiniJson did not parse a valid document");
    Require(value.GetKind() == kb::cli::JsonValue::Kind::Object, "MiniJson root kind is wrong");
    const kb::cli::JsonValue* array = value.Find("a");
    Require(array != nullptr && array->Size() == 3U, "MiniJson array is wrong");
    Require(array->At(0U)->AsNumber() == 1.0, "MiniJson number is wrong");
    Require(array->At(1U)->AsNumber() == -2.5, "MiniJson negative number is wrong");
    Require(array->At(2U)->AsString() == "x\n\xC5\xBE\xC5\xBC", "MiniJson string escapes are wrong");
    const kb::cli::JsonValue* nested = value.Find("b");
    Require(nested != nullptr && nested->Find("c")->AsBool(), "MiniJson nested bool is wrong");
    Require(nested->Find("d")->IsNull(), "MiniJson null is wrong");

    const std::string dumped = value.Dump();
    kb::cli::JsonValue reparsed;
    Require(kb::cli::JsonValue::Parse(dumped, reparsed, error), "MiniJson dump did not round-trip");
    Require(reparsed.Find("a")->Size() == 3U, "MiniJson round-trip lost data");

    Require(!kb::cli::JsonValue::Parse("{\"a\":1,}", value, error), "MiniJson accepted a trailing comma document");
    Require(!kb::cli::JsonValue::Parse("[1] junk", value, error), "MiniJson accepted trailing characters");
    Require(!kb::cli::JsonValue::Parse("\"\\q\"", value, error), "MiniJson accepted an invalid escape");
}

void RunArgumentListTests() {
    const std::vector<std::string> raw{ "--project", "p", "--quiet", "file.lua", "--frames", "12" };
    const kb::cli::ArgumentList arguments{ raw, kFlagNames };
    Require(arguments.Errors().empty(), "ArgumentList reported unexpected errors");
    Require(arguments.Option("--project").value_or("") == "p", "ArgumentList option parsing is wrong");
    Require(arguments.Option("--frames").value_or("") == "12", "ArgumentList trailing option parsing is wrong");
    Require(arguments.Flag("--quiet"), "ArgumentList flag parsing is wrong");
    Require(!arguments.Flag("--disabled"), "ArgumentList reports absent flags");
    Require(arguments.Positionals().size() == 1U && arguments.Positionals()[0] == "file.lua", "ArgumentList positional parsing is wrong");

    const std::vector<std::string> broken{ "--project" };
    const kb::cli::ArgumentList brokenArguments{ broken, kFlagNames };
    Require(!brokenArguments.Errors().empty(), "ArgumentList did not flag a value-less option");
}

void PrepareProject() {
    ResetTestRoot();
    const std::filesystem::path root = TestRoot();
    WriteTextFile(root / "Assets" / "Logic" / "Player.lua", R"(
function Ready(self, dt)
    Log("player ready")
end

function Tick(self, dt)
    local x = self:GetProperty("Transform", "localPosition.x")
    self:SetProperty("Transform", "localPosition.x", x + dt)
    Emit("PlayerTicked", { entity = self.entity })
end
)");
    WriteTextFile(root / "Assets" / "Logic" / "Broken.lua", "function Broken(");

    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Player" }));
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Camera" }));
    std::error_code error;
    std::filesystem::create_directories(root / "Assets" / "Scenes", error);
    Require(!error, "kb_cli test scene directory could not be created");
    Require(
        kb::scene::SceneDocumentService::Save(scene, root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
        "kb_cli test scene could not be saved");
}

void RunValidateCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    const CommandRun good = Run(&kb::cli::RunValidateCommand, { "--project", root, "Assets/Logic/Player.lua" });
    Require(good.exitCode == 0, "validate failed for a valid script");
    Require(Contains(good.output, "OK"), "validate did not report OK");

    const CommandRun bad = Run(&kb::cli::RunValidateCommand, { "--project", root, "Assets/Logic/Broken.lua" });
    Require(bad.exitCode == 1, "validate passed a broken script");
    Require(Contains(bad.output, "FAIL"), "validate did not report FAIL");
}

void RunSceneCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    const CommandRun list = Run(&kb::cli::RunSceneListCommand, { "--project", root, "--scene", "Assets/Scenes/Main.21kbscene" });
    Require(list.exitCode == 0, "scene-list failed");
    Require(Contains(list.output, "Player") && Contains(list.output, "Camera"), "scene-list did not print nodes");

    const CommandRun attach = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "Player",
        "--script", "/Game/Logic/Player.lua",
    });
    Require(attach.exitCode == 0, "scene-attach failed");
    Require(Contains(attach.output, "attached /Game/Logic/Player.lua"), "scene-attach did not confirm");

    const CommandRun listAfter = Run(&kb::cli::RunSceneListCommand, { "--project", root, "--scene", "Assets/Scenes/Main.21kbscene" });
    Require(Contains(listAfter.output, "/Game/Logic/Player.lua"), "scene-list does not show the attached behaviour");
    Require(Contains(listAfter.output, "Lua"), "scene-list does not show the behaviour backend");

    const CommandRun missing = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "DoesNotExist",
        "--script", "/Game/Logic/Player.lua",
    });
    Require(missing.exitCode == 1, "scene-attach accepted a missing node");
    Require(Contains(missing.output, "available nodes"), "scene-attach did not list available nodes");
}

void RunRunCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    const CommandRun attach = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "Player",
        "--script", "/Game/Logic/Player.lua",
    });
    Require(attach.exitCode == 0, "run test could not attach the behaviour");

    const CommandRun run = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "3",
    });
    Require(run.exitCode == 0, "run reported diagnostics for a healthy scene");
    Require(Contains(run.output, "[log] player ready"), "run did not capture Log output");
    Require(Contains(run.output, "PlayerTicked"), "run did not report emitted events");
    Require(Contains(run.output, "0 diagnostics"), "run summary is wrong");
}

void PreparePlayerControllerTemplateProject() {
    ResetTestRoot();
    const std::filesystem::path root = TestRoot();
    WriteTextFile(root / "Assets" / "Logic" / "PlayerController.lua", R"(
local speed = 2.0

function Ready(self, dt)
    Log("player ready")
end

function Tick(self, dt)
    local move = Input.Vector2("Move")
    local dx = (move.x or 0.0) * speed * dt
    local dy = (move.y or 0.0) * speed * dt
    Transform.Translate(self.entity, dx, dy, 0.0)
    Log("player tick")
    Emit("PlayerMoved", {})
end
)");

    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Player" }));
    std::error_code error;
    std::filesystem::create_directories(root / "Assets" / "Scenes", error);
    Require(!error, "player controller template scene directory could not be created");
    Require(
        kb::scene::SceneDocumentService::Save(scene, root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
        "player controller template scene could not be saved");
}

// LIB-013: a minimal PlayerController template must actually run end to end
// in Play Mode without any editor dependency. `kb_cli run` is the only
// editor-independent Play Mode surface the engine has today: it loads a
// project + scene and drives a real ScriptRuntimeHost/ScriptRuntimeSceneSystem
// frame loop headless, with zero bgfx/window/editor involvement. This test
// exercises the exact workflow ScriptAgentProjectFiles' generated AGENTS.md
// documents (scene-attach -> validate -> run) against a PlayerController.lua
// that reads Input.Vector2 and drives Transform.Translate, then runs several
// simulated frames through the real CLI commands, in-process.
void RunPlayerControllerTemplateTests() {
    PreparePlayerControllerTemplateProject();
    const std::string root = TestRoot().string();

    const CommandRun validate = Run(&kb::cli::RunValidateCommand, { "--project", root, "Assets/Logic/PlayerController.lua" });
    Require(validate.exitCode == 0, "player controller template script did not validate");
    Require(Contains(validate.output, "OK"), "player controller template validate did not report OK");

    const CommandRun attach = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "Player",
        "--script", "/Game/Logic/PlayerController.lua",
    });
    Require(attach.exitCode == 0, "player controller template could not attach to the scene");

    constexpr int kFrames = 5;
    const CommandRun run = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", std::to_string(kFrames),
    });
    Require(run.exitCode == 0, "player controller template run reported diagnostics");
    Require(Contains(run.output, "[log] player ready"), "player controller template did not run Ready");
    Require(Contains(run.output, "PlayerMoved"), "player controller template did not emit its movement event");
    Require(Contains(run.output, "0 diagnostics"), "player controller template run was not clean");

    std::size_t tickCount = 0;
    for (std::size_t searchPosition = 0;;) {
        const std::size_t found = run.output.find("[log] player tick", searchPosition);
        if (found == std::string::npos) {
            break;
        }
        ++tickCount;
        searchPosition = found + 1;
    }
    Require(tickCount == static_cast<std::size_t>(kFrames), "player controller template did not tick exactly once per simulated frame");
}

void RunApiCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    const CommandRun printed = Run(&kb::cli::RunApiCommand, { "--print", "json" });
    Require(printed.exitCode == 0, "api --print json failed");
    Require(Contains(printed.output, "\"Input.Vector2\""), "api --print json is missing functions");

    const CommandRun initAgent = Run(&kb::cli::RunInitAgentCommand, { "--project", root });
    Require(initAgent.exitCode == 0, "init-agent failed");
    Require(std::filesystem::exists(TestRoot() / "AGENTS.md"), "init-agent did not write AGENTS.md");
    Require(std::filesystem::exists(TestRoot() / ".kb" / "api" / "kb.lua"), "init-agent did not write Lua stubs");
    Require(std::filesystem::exists(TestRoot() / ".luarc.json"), "init-agent did not write .luarc.json");

    // LIB-023: init-agent must also write a manifest with an API hash, and
    // that hash must be stable across two builds of the identical project
    // (no source/asset change between them).
    const std::filesystem::path manifestPath = TestRoot() / ".kb" / "api" / "manifest.json";
    Require(std::filesystem::exists(manifestPath), "init-agent did not write manifest.json");
    std::ifstream manifestStream{ manifestPath, std::ios::binary };
    const std::string manifestContent{ std::istreambuf_iterator<char>{ manifestStream }, std::istreambuf_iterator<char>{} };
    Require(Contains(manifestContent, "\"version\":"), "manifest.json is missing the version field");
    Require(Contains(manifestContent, "\"hash\":"), "manifest.json is missing the hash field");

    const CommandRun initAgentAgain = Run(&kb::cli::RunInitAgentCommand, { "--project", root });
    Require(initAgentAgain.exitCode == 0, "init-agent second run failed");
    std::ifstream manifestStreamAgain{ manifestPath, std::ios::binary };
    const std::string manifestContentAgain{ std::istreambuf_iterator<char>{ manifestStreamAgain }, std::istreambuf_iterator<char>{} };
    Require(manifestContent == manifestContentAgain, "manifest.json hash must be stable across repeated builds of an unchanged project");
}

void RunMcpCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    std::string input;
    input += R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})";
    input += '\n';
    input += R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
    input += '\n';
    input += R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})";
    input += '\n';
    input += R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"validate_script","arguments":{"path":"Assets/Logic/Player.lua"}}})";
    input += '\n';
    input += R"({"jsonrpc":"2.0","id":4,"method":"no/such/method"})";
    input += '\n';

    std::istringstream in{ input };
    std::ostringstream out;
    std::ostringstream err;
    const std::vector<std::string> raw{ "--project", root };
    const kb::cli::ArgumentList arguments{ raw, kFlagNames };
    const int exitCode = kb::cli::RunMcpCommand(arguments, in, kb::cli::CommandIo{ .out = out, .err = err });
    Require(exitCode == 0, "mcp server exited with an error");

    std::vector<std::string> responses;
    std::istringstream lines{ out.str() };
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) {
            responses.push_back(line);
        }
    }
    Require(responses.size() == 4U, "mcp server response count is wrong");

    for (const std::string& response : responses) {
        kb::cli::JsonValue parsed;
        std::string error;
        Require(kb::cli::JsonValue::Parse(response, parsed, error), "mcp server emitted invalid JSON");
    }
    Require(Contains(responses[0], "\"protocolVersion\""), "mcp initialize response is wrong");
    Require(Contains(responses[1], "\"tools\""), "mcp tools/list response is wrong");
    Require(Contains(responses[1], "scene_attach"), "mcp tools/list is missing tools");
    Require(Contains(responses[2], "OK") && Contains(responses[2], "\"isError\":false"), "mcp validate_script call failed");
    Require(Contains(responses[3], "-32601"), "mcp unknown method error is wrong");
}

} // namespace

int main() {
    RunMiniJsonTests();
    RunArgumentListTests();
    RunValidateCommandTests();
    RunSceneCommandTests();
    RunRunCommandTests();
    RunPlayerControllerTemplateTests();
    RunApiCommandTests();
    RunMcpCommandTests();
    return EXIT_SUCCESS;
}

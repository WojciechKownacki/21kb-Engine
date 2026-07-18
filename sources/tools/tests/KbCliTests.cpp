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

constexpr std::array<std::string_view, 3> kFlagNames{ "--disabled", "--quiet", "--update-baseline" };

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

// LIB-013: PlayerController.lua is no longer duplicated as a hardcoded
// string in this test fixture — it is written by the REAL production path
// a game author actually gets it through, `kb_cli init-agent`
// (ScriptAgentProjectFiles::Write), the exact same file AGENTS.md's own
// worked example already references. This closes the "no shipped template,
// only a test fixture" audit gap: what this test exercises below IS the
// file init-agent produces, not a second, driftable copy of similar text.
void PreparePlayerControllerTemplateProject() {
    ResetTestRoot();
    const std::filesystem::path root = TestRoot();

    const CommandRun initAgent = Run(&kb::cli::RunInitAgentCommand, { "--project", root.string() });
    Require(initAgent.exitCode == 0, "player controller template project init-agent failed");
    Require(std::filesystem::exists(root / "Assets" / "Logic" / "PlayerController.lua"),
        "kb_cli init-agent did not write the real PlayerController.lua template");

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
// documents (init-agent -> scene-attach -> validate -> run) against the REAL,
// shipped PlayerController.lua template (LIB-013 — written by init-agent
// itself, not a second hardcoded copy of similar text) that reads
// Input.Vector2 and drives Transform.Translate, then runs several simulated
// frames through the real CLI commands, in-process. Proves lifecycle
// callbacks/events fire correctly headless; RunPlayerControllerTemplateMoves
// TransformWithRealInputTest (ScriptRuntimeTests.cpp) proves the SAME
// shipped file actually moves the entity for real, non-zero input — this
// harness has no channel to observe Transform state (see that test's own
// comment for why).
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

// LIB-014: Projectile.lua (and its Assets/Prefabs/Projectile.kbprefab
// companion) is no longer duplicated as a hardcoded string in this test
// fixture — same fix as LIB-013's PlayerController, same reasoning: it is
// written by the REAL production path a game author actually gets it
// through, `kb_cli init-agent` (ScriptAgentProjectFiles::Write). This closes
// the "no shipped template, only a test fixture" gap for the script itself;
// the shipped prefab artifact is asserted below and proven under real Jolt
// physics in PhysicsSceneSystemTests.cpp's LIB-014 section.
void PrepareProjectileTemplateProject() {
    ResetTestRoot();
    const std::filesystem::path root = TestRoot();

    const CommandRun initAgent = Run(&kb::cli::RunInitAgentCommand, { "--project", root.string() });
    Require(initAgent.exitCode == 0, "projectile template project init-agent failed");
    Require(std::filesystem::exists(root / "Assets" / "Logic" / "Projectile.lua"),
        "kb_cli init-agent did not write the real Projectile.lua template");
    Require(std::filesystem::exists(root / "Assets" / "Prefabs" / "Projectile.kbprefab"),
        "kb_cli init-agent did not write the real Projectile.kbprefab artifact");

    kb::scene::Scene scene;
    static_cast<void>(scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Projectile" }));
    std::error_code error;
    std::filesystem::create_directories(root / "Assets" / "Scenes", error);
    Require(!error, "projectile template scene directory could not be created");
    Require(
        kb::scene::SceneDocumentService::Save(scene, root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
        "projectile template scene could not be saved");
}

// LIB-014: a minimal Projectile template - movement via Physics.SetVelocity
// (LIB-124), collision via OnCollisionEnter (LIB-127), destruction via
// World.Destroy - run through the SAME editor-independent `kb_cli run`
// workflow LIB-013's PlayerController template established. `kb_cli run`
// does not load a project's physics plugin (MountProjectAssets only mounts
// assets; CliRunCommand.cpp constructs a plain Scene with no
// ProjectDescriptor at all) - a real, separate, out-of-scope infrastructure
// gap discovered while writing this test (extending the CLI to load
// project-configured plugins is a genuinely different task, not "add a
// template"). This test therefore proves what IS true through this
// harness: the template is syntactically valid, runs cleanly (0
// diagnostics) with no physics backend registered, Ready fires every frame
// it should, and Physics.SetVelocity fails honestly (applied=false, no
// crash - exactly IPhysicsBackend's documented no-backend contract) rather
// than ever fabricating a launch. The REAL physics-driven proof - the
// projectile actually moving, hitting a target, launching, and being
// destroyed - is in PhysicsSceneSystemTests.cpp's
// RunPhysicsSceneSystemFallingBodyTest (LIB-014 section), which drives a
// real Jolt-backed Scene directly (the same ScriptRuntimeHost/frame-loop
// machinery kb_cli run itself wraps, without the CLI process boundary).
void RunProjectileTemplateTests() {
    PrepareProjectileTemplateProject();
    const std::string root = TestRoot().string();

    const CommandRun validate = Run(&kb::cli::RunValidateCommand, { "--project", root, "Assets/Logic/Projectile.lua" });
    Require(validate.exitCode == 0, "projectile template script did not validate");
    Require(Contains(validate.output, "OK"), "projectile template validate did not report OK");

    const CommandRun attach = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "Projectile",
        "--script", "/Game/Logic/Projectile.lua",
    });
    Require(attach.exitCode == 0, "projectile template could not attach to the scene");

    const CommandRun run = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "5",
    });
    Require(run.exitCode == 0, "projectile template run reported diagnostics");
    Require(Contains(run.output, "[log] projectile ready"), "projectile template did not run Ready");
    Require(!Contains(run.output, "[log] projectile launched"),
        "projectile template must NOT report a launch when kb_cli run has no physics backend loaded - Physics.SetVelocity must keep honestly failing, never fabricate success");
    Require(Contains(run.output, "0 diagnostics"), "projectile template run was not clean even though Physics.SetVelocity fails every frame");
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
    Require(std::filesystem::exists(TestRoot() / "Assets" / "Logic" / "PlayerController.lua"),
        "init-agent did not write the PlayerController.lua template (LIB-013)");
    Require(std::filesystem::exists(TestRoot() / "Assets" / "Logic" / "Projectile.lua"),
        "init-agent did not write the Projectile.lua template (LIB-014)");
    Require(std::filesystem::exists(TestRoot() / "Assets" / "Prefabs" / "Projectile.kbprefab"),
        "init-agent did not write the Projectile.kbprefab artifact (LIB-014)");

    // LIB-013 regression: init-agent internally rebuilds its catalog and
    // calls ScriptAgentProjectFiles::Write() a second time whenever the
    // first call created a new project asset (PlayerController.lua, on a
    // project's first-ever run) — a bug once made that second call's
    // report silently replace the first's, so a genuinely first-time
    // AGENTS.md/.luarc.json/PlayerController.lua write was wrongly printed
    // as "kept ... (already exists)" even though this SAME invocation had
    // just created it moments earlier.
    Require(Contains(initAgent.output, "wrote") && Contains(initAgent.output, "AGENTS.md"), "init-agent's own first-ever run must report AGENTS.md as written, not kept");
    Require(!Contains(initAgent.output, "kept") && !Contains(initAgent.output, "already exists"),
        "init-agent must not report a file it just created in THIS SAME first-ever invocation as already existing");

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

// LIB-024: api-check must generate a baseline, pass an identical surface,
// flag a breaking change (removed function / changed pin contract) with a
// non-zero exit, and treat a purely-additive difference as compatible.
// Exercised through the real RunApiCheckCommand against the real,
// project-agnostic engine catalog (no --project) — the same path CI runs.
void RunApiCheckCommandTests() {
    ResetTestRoot();
    const std::string baseline = (TestRoot() / "baseline.json").string();

    // A missing baseline is an honest error, not a silent pass.
    const CommandRun missing = Run(&kb::cli::RunApiCheckCommand, { "--baseline", baseline });
    Require(missing.exitCode == 1, "api-check must fail when the baseline file is missing");

    // --update-baseline writes the current surface as the new baseline.
    const CommandRun update = Run(&kb::cli::RunApiCheckCommand, { "--baseline", baseline, "--update-baseline" });
    Require(update.exitCode == 0, "api-check --update-baseline must succeed");
    Require(std::filesystem::exists(TestRoot() / "baseline.json"), "api-check --update-baseline must write the baseline file");

    // An unchanged surface is compatible.
    const CommandRun identical = Run(&kb::cli::RunApiCheckCommand, { "--baseline", baseline });
    Require(identical.exitCode == 0, "api-check must pass an identical API surface");
    Require(Contains(identical.output, "compatible"), "api-check must report compatibility for an identical surface");

    // Read the generated baseline so the breaking/additive fixtures below
    // are edits of the REAL catalog, not hand-invented shapes that might
    // not match what the engine actually registers.
    std::ifstream baselineStream{ TestRoot() / "baseline.json", std::ios::binary };
    const std::string baselineText{ std::istreambuf_iterator<char>{ baselineStream }, std::istreambuf_iterator<char>{} };
    Require(Contains(baselineText, "\"World.Exists\""), "api-check baseline is missing the expected World.Exists function");

    // BREAKING (removed function): a baseline claiming a function the
    // current surface does not have.
    {
        std::string breaking = baselineText;
        const std::string anchor = "\"functions\":[";
        const std::size_t pos = breaking.find(anchor);
        Require(pos != std::string::npos, "api-check baseline has no functions array");
        breaking.insert(pos + anchor.size(), R"({"name":"Ghost.Removed","inputs":[],"outputs":[]},)");
        const std::string breakingPath = (TestRoot() / "breaking.json").string();
        WriteTextFile(breakingPath, breaking);
        const CommandRun run = Run(&kb::cli::RunApiCheckCommand, { "--baseline", breakingPath });
        Require(run.exitCode == 1, "api-check must fail on a removed function");
        Require(Contains(run.output, "BREAKING") && Contains(run.output, "Ghost.Removed"), "api-check must name the removed function as breaking");
    }

    // BREAKING (changed output contract): World.Exists's "exists" output
    // retyped Bool -> Int.
    {
        std::string pinChange = baselineText;
        const std::string needle = R"("name":"World.Exists","inputs":[{"name":"entity","type":"Entity","required":true}],"outputs":[{"name":"exists","type":"Bool")";
        const std::size_t pos = pinChange.find(needle);
        Require(pos != std::string::npos, "api-check baseline World.Exists shape changed unexpectedly");
        const std::string replacement = R"("name":"World.Exists","inputs":[{"name":"entity","type":"Entity","required":true}],"outputs":[{"name":"exists","type":"Int")";
        pinChange.replace(pos, needle.size(), replacement);
        const std::string pinChangePath = (TestRoot() / "pinchange.json").string();
        WriteTextFile(pinChangePath, pinChange);
        const CommandRun run = Run(&kb::cli::RunApiCheckCommand, { "--baseline", pinChangePath });
        Require(run.exitCode == 1, "api-check must fail on a changed output contract");
        Require(Contains(run.output, "BREAKING") && Contains(run.output, "World.Exists"), "api-check must name the changed function as breaking");
    }

    // ADDITIVE (baseline lacks a function the current surface has): the
    // current-only function is additive, never breaking. Removing the
    // known World.Exists object (rather than trying to empty the whole
    // functions array, whose nested inputs/outputs brackets make naive
    // bracket-matching unsafe) is the minimal, robust way to construct this.
    {
        std::string additive = baselineText;
        const std::string worldExists = R"({"name":"World.Exists","inputs":[{"name":"entity","type":"Entity","required":true}],"outputs":[{"name":"exists","type":"Bool","required":true}]})";
        const std::size_t pos = additive.find(worldExists);
        Require(pos != std::string::npos, "api-check baseline World.Exists object shape changed unexpectedly");
        // Remove the object plus one adjacent comma so the array stays valid
        // JSON whether World.Exists was first, middle, or last.
        std::size_t removeStart = pos;
        std::size_t removeLength = worldExists.size();
        if (pos + worldExists.size() < additive.size() && additive[pos + worldExists.size()] == ',') {
            removeLength += 1; // trailing comma
        } else if (pos > 0 && additive[pos - 1] == ',') {
            removeStart -= 1; // leading comma (World.Exists was the last entry)
            removeLength += 1;
        }
        additive.erase(removeStart, removeLength);
        const std::string additivePath = (TestRoot() / "additive.json").string();
        WriteTextFile(additivePath, additive);
        const CommandRun run = Run(&kb::cli::RunApiCheckCommand, { "--baseline", additivePath });
        Require(run.exitCode == 0, "api-check must treat a purely-additive difference as compatible");
        Require(Contains(run.output, "additive") && Contains(run.output, "World.Exists"), "api-check must report the added function as additive");
    }
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
    RunProjectileTemplateTests();
    RunApiCommandTests();
    RunApiCheckCommandTests();
    RunMcpCommandTests();
    return EXIT_SUCCESS;
}

#include "CliCommands.hpp"
#include "MiniJson.hpp"

#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
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

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#define KB_PHYSICS_JOLT_PLUGIN_PATH ""
#endif

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

void WriteProjectDescriptor(const std::filesystem::path& root, bool withPhysics) {
    kb::project::ProjectDescriptor descriptor;
    descriptor.name = "KbCliRuntimeTest";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    if (withPhysics) {
        Require(
            !std::filesystem::path{ KB_PHYSICS_JOLT_PLUGIN_PATH }.empty(),
            "kb_cli physics runtime test requires a dynamic Jolt plugin");
        descriptor.plugins.push_back(kb::project::ProjectPluginReference{
            .name = "Physics.Jolt",
            // Match the portable filename persisted by the real editor. The
            // production loader, not an absolute test-only path, must resolve
            // the current Debug/Release build layout.
            .binaryPath = std::filesystem::path{ KB_PHYSICS_JOLT_PLUGIN_PATH }.filename().string(),
            .enabled = true,
        });
    }
    Require(
        kb::project::ProjectManager::SaveProject(root / "Project.21kbproject", descriptor),
        "kb_cli test project descriptor could not be written");
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
    WriteProjectDescriptor(root, false);
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

    kb::project::ProjectDescriptor brokenDescriptor;
    brokenDescriptor.name = "KbCliMissingPluginTest";
    brokenDescriptor.plugins.push_back(kb::project::ProjectPluginReference{
        .name = "Physics.Missing",
        .binaryPath = "kb_plugin_that_does_not_exist.dll",
        .enabled = true,
    });
    Require(
        kb::project::ProjectManager::SaveProject(
            TestRoot() / "Project.21kbproject",
            brokenDescriptor),
        "kb_cli missing-plugin descriptor could not be written");
    const CommandRun missingPlugin = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "1",
    });
    Require(missingPlugin.exitCode == 1,
        "run silently continued when a project-configured plugin could not load");
    Require(Contains(missingPlugin.output, "[module error]"),
        "run did not expose the project plugin load diagnostic");
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
    WriteProjectDescriptor(root, false);

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
    WriteProjectDescriptor(root, true);

    const CommandRun initAgent = Run(&kb::cli::RunInitAgentCommand, { "--project", root.string() });
    Require(initAgent.exitCode == 0, "projectile template project init-agent failed");
    Require(std::filesystem::exists(root / "Assets" / "Logic" / "Projectile.lua"),
        "kb_cli init-agent did not write the real Projectile.lua template");
    Require(std::filesystem::exists(root / "Assets" / "Prefabs" / "Projectile.kbprefab"),
        "kb_cli init-agent did not write the real Projectile.kbprefab artifact");

    // An independent observer proves the Jolt update wrote its simulated pose
    // back to Transform in the production CLI frame loop; a successful
    // Physics.SetVelocity call alone would only prove API registration.
    WriteTextFile(root / "Assets" / "Logic" / "PhysicsRuntimeProbe.lua", R"(
local controlsApplied = false
local projectileReported = false
local forceReported = false
local impulseReported = false
local velocityReported = false
local angularReported = false
local kinematicReported = false
local sleepApplied = false
local sleepFrames = 0
local sleepInitialX = 0.0
local sleepHeld = false
local wakeApplied = false
local wakeReported = false
local negativeCasesReported = false
local completed = false
local queriesReported = false
local characterMoveApplied = false
local characterReported = false
local jointImpulseApplied = false
local jointReported = false
local lib123Completed = false

function Tick(self, dt)
    local projectile = World.FindByName("Projectile")
    local forceBody = World.FindByName("ForceBody")
    local impulseBody = World.FindByName("ImpulseBody")
    local velocityBody = World.FindByName("VelocityBody")
    local kinematicBody = World.FindByName("KinematicBody")
    local sleepBody = World.FindByName("SleepBody")
    local queryTarget = World.FindByName("QueryTarget")
    local character = World.FindByName("CharacterProbe")
    local jointOwner = World.FindByName("JointOwner")
    local jointConnected = World.FindByName("JointConnected")

    if not controlsApplied then
        local forceApplied = Physics.AddForce(forceBody, 60.0, 0.0, 0.0)
        local impulseApplied = Physics.AddImpulse(impulseBody, 2.0, 0.0, 0.0)
        local impulseVelocity = Physics.GetVelocity(impulseBody)
        local velocitySet = Physics.SetVelocity(velocityBody, 6.0, 0.0, 0.0)
        local velocityReadback = Physics.GetVelocity(velocityBody)
        local angularSet = Physics.SetAngularVelocity(impulseBody, 0.0, 0.0, 3.0)
        local angularVelocity = Physics.GetAngularVelocity(impulseBody)
        local kinematicMoved = Physics.MoveKinematic(kinematicBody, 9.0, 0.0, 3.0)
        if forceApplied and impulseApplied and impulseVelocity.found and impulseVelocity.x > 1.5
            and velocitySet and velocityReadback.found and velocityReadback.x > 5.5
            and angularSet and angularVelocity.found and angularVelocity.z > 2.5
            and kinematicMoved then
            controlsApplied = true
            Log("physics runtime controls applied")
        end
    end

    if not projectileReported then
        local position = Transform.GetPosition(projectile)
        if position ~= nil and position.x > 0.1 then
            projectileReported = true
            Log("physics runtime body moved")
        end
    end

    if controlsApplied and not forceReported then
        local position = Transform.GetPosition(forceBody)
        if position ~= nil and position.x > 3.25 then
            forceReported = true
            Log("physics runtime force integrated")
        end
    end

    -- The impulse and SetVelocity operations must produce write-back motion,
    -- not merely succeed or return a cached vector in the call frame.
    if controlsApplied and not impulseReported then
        local position = Transform.GetPosition(impulseBody)
        if position ~= nil and position.x > 6.25 then
            impulseReported = true
            Log("physics runtime impulse integrated")
        end
    end
    if controlsApplied and not velocityReported then
        local position = Transform.GetPosition(velocityBody)
        if position ~= nil and position.x > 30.5 then
            velocityReported = true
            Log("physics runtime velocity moved")
        end
    end
    -- Lua has no Transform rotation reader, so the backend's next-frame
    -- angular-velocity read is the observable state here. Reading it after a
    -- real Jolt step proves it survived the step instead of being a wrapper
    -- return value from SetAngularVelocity.
    if controlsApplied and not angularReported then
        local angularVelocity = Physics.GetAngularVelocity(impulseBody)
        if angularVelocity.found and angularVelocity.z > 2.5 then
            angularReported = true
            Log("physics runtime angular velocity persisted")
        end
    end

    if controlsApplied and not kinematicReported then
        local position = Transform.GetPosition(kinematicBody)
        if position ~= nil and position.z > 2.5 then
            kinematicReported = true
            Log("physics runtime kinematic moved")
        end
    end

    -- Sleep must halt an already-velocity-bearing live body over multiple
    -- fixed steps; Wake must subsequently let a new impulse move it. Jolt
    -- may clear an inactive body's velocity, so the impulse is intentionally
    -- applied after Wake rather than assuming an undocumented preservation
    -- policy for sleeping velocity.
    if controlsApplied and not sleepApplied then
        local position = Transform.GetPosition(sleepBody)
        local velocitySet = Physics.SetVelocity(sleepBody, 6.0, 0.0, 0.0)
        local slept = Physics.Sleep(sleepBody)
        if position ~= nil and velocitySet and slept and Physics.IsSleeping(sleepBody) then
            sleepInitialX = position.x
            sleepApplied = true
            Log("physics runtime sleep applied")
        end
    elseif sleepApplied and not sleepHeld and not wakeApplied then
        sleepFrames = sleepFrames + 1
        local position = Transform.GetPosition(sleepBody)
        if sleepFrames >= 3 and position ~= nil and math.abs(position.x - sleepInitialX) < 0.01
            and Physics.IsSleeping(sleepBody) then
            sleepHeld = true
            Log("physics runtime sleep held body")
        end
    end
    if sleepHeld and not wakeApplied and Physics.Wake(sleepBody)
        and Physics.AddImpulse(sleepBody, 2.0, 0.0, 0.0) and not Physics.IsSleeping(sleepBody) then
        wakeApplied = true
        Log("physics runtime wake applied")
    end
    if wakeApplied and not wakeReported then
        local position = Transform.GetPosition(sleepBody)
        if position ~= nil and position.x > sleepInitialX + 0.25 then
            wakeReported = true
            Log("physics runtime wake moved body")
        end
    end

    -- Negative calls use a real loaded backend but an entity with no live
    -- body. MoveKinematic additionally rejects a live Dynamic body. These
    -- must fail honestly and must not emit a script diagnostic.
    if controlsApplied and not negativeCasesReported then
        local invalidVelocity = Physics.GetVelocity(0)
        local invalidAngularVelocity = Physics.GetAngularVelocity(0)
        local staticVelocity = Physics.GetVelocity(queryTarget)
        local staticAngularVelocity = Physics.GetAngularVelocity(queryTarget)
        local kinematicVelocity = Physics.GetVelocity(kinematicBody)
        local kinematicAngularVelocity = Physics.GetAngularVelocity(kinematicBody)
        if not Physics.AddForce(0, 1.0, 0.0, 0.0)
            and not Physics.AddImpulse(0, 1.0, 0.0, 0.0)
            and not Physics.SetVelocity(0, 1.0, 0.0, 0.0)
            and invalidVelocity ~= nil and not invalidVelocity.found
            and not Physics.SetAngularVelocity(0, 0.0, 0.0, 1.0)
            and invalidAngularVelocity ~= nil and not invalidAngularVelocity.found
            and not Physics.Sleep(0) and not Physics.Wake(0) and not Physics.IsSleeping(0) then
            -- A loaded backend must also reject the wrong *live* body kind,
            -- not only an unknown entity. Rigidbody controls are Dynamic
            -- only; Kinematic bodies accept only MoveKinematic.
            local staticRejected = not Physics.AddForce(queryTarget, 1.0, 0.0, 0.0)
                and not Physics.AddImpulse(queryTarget, 1.0, 0.0, 0.0)
                and not Physics.SetVelocity(queryTarget, 1.0, 0.0, 0.0)
                and staticVelocity ~= nil and not staticVelocity.found
                and not Physics.SetAngularVelocity(queryTarget, 0.0, 0.0, 1.0)
                and staticAngularVelocity ~= nil and not staticAngularVelocity.found
                and not Physics.Sleep(queryTarget) and not Physics.Wake(queryTarget) and not Physics.IsSleeping(queryTarget)
                and not Physics.MoveKinematic(queryTarget, 15.0, 0.0, 0.0)
            local kinematicRejected = not Physics.AddForce(kinematicBody, 1.0, 0.0, 0.0)
                and not Physics.AddImpulse(kinematicBody, 1.0, 0.0, 0.0)
                and not Physics.SetVelocity(kinematicBody, 1.0, 0.0, 0.0)
                and kinematicVelocity ~= nil and not kinematicVelocity.found
                and not Physics.SetAngularVelocity(kinematicBody, 0.0, 0.0, 1.0)
                and kinematicAngularVelocity ~= nil and not kinematicAngularVelocity.found
                and not Physics.Sleep(kinematicBody) and not Physics.Wake(kinematicBody) and not Physics.IsSleeping(kinematicBody)
            if staticRejected and kinematicRejected and not Physics.MoveKinematic(forceBody, 0.0, 0.0, 0.0) then
                negativeCasesReported = true
                Log("physics runtime controls reject invalid bodies")
            end
        end
    end

    -- CharacterMove only returns true once the Jolt fixed system has created
    -- its real CharacterVirtual. The later Transform read proves that the
    -- virtual character, not merely its Lua binding, wrote a simulated pose.
    if not characterMoveApplied and Physics.CharacterMove(character, 3.0, 0.0) then
        characterMoveApplied = true
        Log("physics runtime character move applied")
    end
    if characterMoveApplied and not characterReported then
        local position = Transform.GetPosition(character)
        local velocity = Physics.CharacterVelocity(character)
        if position ~= nil and position.x > 0.25
            and velocity.found and velocity.x > 2.5 then
            characterReported = true
            Log("physics runtime character virtual moved")
        end
    end

    -- The owner is impulsed only after live Jolt bodies exist. A fixed joint
    -- with matching world anchors must move the connected body while keeping
    -- their two-unit separation; an unconsumed/missing JointComponent cannot
    -- satisfy both observations.
    if not jointImpulseApplied and Physics.AddImpulse(jointOwner, 8.0, 0.0, 0.0) then
        jointImpulseApplied = true
        Log("physics runtime joint impulse applied")
    end
    if jointImpulseApplied and not jointReported then
        local ownerPosition = Transform.GetPosition(jointOwner)
        local connectedPosition = Transform.GetPosition(jointConnected)
        if ownerPosition ~= nil and connectedPosition ~= nil
            and connectedPosition.x > 23.25
            and math.abs((connectedPosition.x - ownerPosition.x) - 2.0) < 0.2 then
            jointReported = true
            Log("physics runtime joint constrained")
        end
    end

    if not completed and projectileReported and forceReported and impulseReported and velocityReported
        and angularReported and kinematicReported and sleepHeld and wakeReported and negativeCasesReported then
        completed = true
        Log("physics runtime LIB-124 complete")
    end

    if not queriesReported then
        local ray = Physics.Raycast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 8)
        local sphere = Physics.SphereCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 8)
        local box = Physics.BoxCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.2, 0.2, 8)
        local capsule = Physics.CapsuleCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.8, 8)
        local overlapSphere = Physics.OverlapSphere(15.0, 0.0, 0.0, 0.75, 8)
        local overlapBox = Physics.OverlapBox(15.0, 0.0, 0.0, 0.75, 0.75, 0.75, 8)
        local overlapCapsule = Physics.OverlapCapsule(15.0, 0.0, 0.0, 0.4, 1.5, 8)
        local closest = Physics.ClosestPoint(queryTarget, 15.0, 3.0, 0.0, 8)
        -- Every query must reject the same real body when its layer is not
        -- selected. This is intentionally not an empty-world test: mask 4
        -- differs from QueryTarget's serialized layer 8 while all geometry
        -- remains identical to the positive calls above.
        local maskedRay = Physics.Raycast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 4)
        local maskedSphere = Physics.SphereCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 4)
        local maskedBox = Physics.BoxCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.2, 0.2, 4)
        local maskedCapsule = Physics.CapsuleCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.8, 4)
        local maskedOverlapSphere = Physics.OverlapSphere(15.0, 0.0, 0.0, 0.75, 4)
        local maskedOverlapBox = Physics.OverlapBox(15.0, 0.0, 0.0, 0.75, 0.75, 0.75, 4)
        local maskedOverlapCapsule = Physics.OverlapCapsule(15.0, 0.0, 0.0, 0.4, 1.5, 4)
        local maskedClosest = Physics.ClosestPoint(queryTarget, 15.0, 3.0, 0.0, 4)
        -- PhysicsObserver is a valid scene entity but intentionally has no
        -- Rigidbody/Collider, so ClosestPoint must return its honest miss
        -- result through the live backend without a script diagnostic.
        local invalidClosest = Physics.ClosestPoint(self.entity, 15.0, 3.0, 0.0)
        if ray.hit and ray.entity == queryTarget
            and sphere.hit and sphere.entity == queryTarget
            and box.hit and box.entity == queryTarget
            and capsule.hit and capsule.entity == queryTarget
            and overlapSphere.overlapping and overlapSphere.entity == queryTarget
            and overlapBox.overlapping and overlapBox.entity == queryTarget
            and overlapCapsule.overlapping and overlapCapsule.entity == queryTarget
            and closest.found and closest.distance > 2.4 and closest.distance < 2.6
            and not maskedRay.hit and not maskedSphere.hit and not maskedBox.hit
            and not maskedCapsule.hit and not maskedOverlapSphere.overlapping
            and not maskedOverlapBox.overlapping and not maskedOverlapCapsule.overlapping
            and not maskedClosest.found and not invalidClosest.found then
            queriesReported = true
            Log("physics runtime LIB-125 complete")
        end
    end

    if not lib123Completed and characterReported and jointReported then
        lib123Completed = true
        Log("physics runtime LIB-123 complete")
    end
end
)");

    kb::scene::Scene scene;
    const kb::scene::SceneObject projectile =
        scene.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Projectile" });
    scene.Components().Rigidbodies().Set(
        projectile.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
            .useGravity = false,
        });
    scene.Components().Colliders().Set(
        projectile.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.25F,
            .friction = 0.35F,
            .restitution = 0.2F,
        });

    const auto createProbeBody = [&scene](
                                     const char* name,
                                     kb::scene::RigidbodyBodyType bodyType,
                                     float x) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ x, 0.0F, 0.0F },
                },
            });
        scene.Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = bodyType,
                .mass = 1.0F,
                .useGravity = false,
            });
        scene.Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.25F,
            });
        return object;
    };
    createProbeBody("ForceBody", kb::scene::RigidbodyBodyType::Dynamic, 3.0F);
    createProbeBody("ImpulseBody", kb::scene::RigidbodyBodyType::Dynamic, 6.0F);
    createProbeBody("KinematicBody", kb::scene::RigidbodyBodyType::Kinematic, 9.0F);
    createProbeBody("VelocityBody", kb::scene::RigidbodyBodyType::Dynamic, 30.0F);
    createProbeBody("SleepBody", kb::scene::RigidbodyBodyType::Dynamic, 36.0F);
    const kb::scene::SceneObject queryTarget = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "QueryTarget",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 15.0F, 0.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        queryTarget.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Static,
        });
    scene.Components().Colliders().Set(
        queryTarget.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{ 1.0F, 1.0F, 1.0F },
            .layer = 8U,
        });

    const kb::scene::SceneObject character = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "CharacterProbe",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 0.0F, 4.0F, 4.0F },
            },
        });
    scene.Components().CharacterControllers().Set(
        character.Entity(),
        kb::scene::CharacterControllerComponent{
            .radius = 0.4F,
            .height = 1.8F,
            .useGravity = false,
        });

    const auto createJointBody = [&scene](const char* name, float x) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ x, 0.0F, 4.0F },
                },
            });
        scene.Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 1.0F,
                .useGravity = false,
            });
        scene.Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.25F,
            });
        return object;
    };
    const kb::scene::SceneObject jointOwner = createJointBody("JointOwner", 21.0F);
    const kb::scene::SceneObject jointConnected = createJointBody("JointConnected", 23.0F);
    scene.Components().Joints().Set(
        jointOwner.Entity(),
        kb::scene::JointComponent{
            .type = kb::scene::JointType::Fixed,
            .connectedEntity = jointConnected.Entity(),
            .anchor = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            .connectedAnchor = kb::scene::Vec3{ -1.0F, 0.0F, 0.0F },
        });
    static_cast<void>(scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{ .name = "PhysicsObserver" }));
    std::error_code error;
    std::filesystem::create_directories(root / "Assets" / "Scenes", error);
    Require(!error, "projectile template scene directory could not be created");
    Require(
        kb::scene::SceneDocumentService::Save(scene, root / "Assets" / "Scenes" / "Main.21kbscene", "Main"),
        "projectile template scene could not be saved");
}

// LIB-014/LIB-123: the shipped Projectile template runs through the same
// editor-independent production `kb_cli run` workflow as a built game tool:
// Project.21kbproject -> dynamic Physics.Jolt module -> serialized
// Rigidbody/Collider/CharacterController/Joint -> Jolt fixed update ->
// Transform write-back -> Lua.
// The observer above makes the final write-back visible in the command log.
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

    const CommandRun attachProbe = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "PhysicsObserver",
        "--script", "/Game/Logic/PhysicsRuntimeProbe.lua",
    });
    Require(attachProbe.exitCode == 0, "physics runtime observer could not attach to the scene");

    const CommandRun run = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "40",
    });
    Require(run.exitCode == 0, "projectile template run reported diagnostics");
    Require(Contains(run.output, "[module] active Physics.Jolt"),
        "kb_cli run did not load the project-configured Physics.Jolt module");
    Require(Contains(run.output, "[log] projectile ready"), "projectile template did not run Ready");
    Require(Contains(run.output, "[log] projectile launched"),
        "projectile template did not reach the live Jolt rigid body");
    Require(Contains(run.output, "[log] physics runtime body moved"),
        "Jolt did not write the simulated body pose back to Transform in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime controls applied"),
        "full LIB-124 rigidbody control API did not apply through live Jolt");
    Require(Contains(run.output, "[log] physics runtime force integrated"),
        "Jolt did not integrate Physics.AddForce in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime impulse integrated"),
        "Jolt did not integrate Physics.AddImpulse into observable motion in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime velocity moved"),
        "Jolt did not apply Physics.SetVelocity to observable motion in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime angular velocity persisted"),
        "Jolt did not retain Physics.SetAngularVelocity across a fixed step in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime kinematic moved"),
        "Jolt did not execute Physics.MoveKinematic in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime sleep held body"),
        "Physics.Sleep did not halt a live body across fixed steps in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime wake moved body"),
        "Physics.Wake did not resume a live body's motion in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime controls reject invalid bodies"),
        "LIB-124 calls did not reject invalid/non-kinematic bodies honestly in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-124 complete"),
        "LIB-124 production runtime probe did not complete");
    Require(Contains(run.output, "[log] physics runtime LIB-125 complete"),
        "LIB-125 cast/overlap/closest-point production runtime probe did not complete");
    Require(Contains(run.output, "[log] physics runtime character virtual moved"),
        "Jolt CharacterVirtual did not move and write its pose back in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime joint constrained"),
        "serialized JointComponent did not constrain live Jolt bodies in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-123 complete"),
        "LIB-123 character/joint production runtime probe did not complete");
    Require(Contains(run.output, "0 diagnostics"), "projectile physics runtime was not clean");
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
        const std::size_t functionPos = pinChange.find(R"("name":"World.Exists")");
        const std::string needle = R"("name":"exists","type":"Bool")";
        const std::size_t pos = pinChange.find(needle, functionPos);
        Require(pos != std::string::npos, "api-check baseline World.Exists shape changed unexpectedly");
        const std::string replacement = R"("name":"exists","type":"Int")";
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
        const std::size_t pos = additive.find(R"({"name":"World.Exists")");
        Require(pos != std::string::npos, "api-check baseline World.Exists object shape changed unexpectedly");
        const std::size_t objectEnd = additive.find("}]}", pos);
        Require(objectEnd != std::string::npos, "api-check baseline World.Exists object has no output boundary");
        const std::size_t worldExistsSize = objectEnd + 3U - pos;
        // Remove the object plus one adjacent comma so the array stays valid
        // JSON whether World.Exists was first, middle, or last.
        std::size_t removeStart = pos;
        std::size_t removeLength = worldExistsSize;
        if (pos + worldExistsSize < additive.size() && additive[pos + worldExistsSize] == ',') {
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
    RunApiCommandTests();
    RunApiCheckCommandTests();
    RunMcpCommandTests();
    // Keep the production physics fixture on disk after the test process so
    // the built kb_cli executable can be run against it as a separate-process
    // runtime verification.
    RunProjectileTemplateTests();
    return EXIT_SUCCESS;
}

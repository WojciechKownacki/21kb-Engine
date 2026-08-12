#include "CliCommands.hpp"
#include "engine/core/JsonValue.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/input/InputAssetIO.hpp"
#include "engine/input/InputKey.hpp"
#include "engine/project/ProjectManager.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneDocumentService.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneHierarchyAccess.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/scene/PhysicsLayersAssetIO.hpp"
#include "engine/scene/SceneTransforms.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(KB_PHYSICS_JOLT_PLUGIN_PATH)
#define KB_PHYSICS_JOLT_PLUGIN_PATH ""
#endif

namespace {
using kb::core::JsonValue;

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

void WriteProjectDescriptor(
    const std::filesystem::path& root,
    bool withPhysics,
    std::string physicsLayersAsset = {}) {
    kb::project::ProjectDescriptor descriptor;
    descriptor.name = "KbCliRuntimeTest";
    descriptor.defaultScene = "/Game/Scenes/Main.21kbscene";
    descriptor.physicsLayersAsset = std::move(physicsLayersAsset);
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

[[nodiscard]] std::string LineContaining(const std::string& text, std::string_view needle) {
    const std::size_t match = text.find(needle);
    if (match == std::string::npos) {
        return {};
    }
    const std::size_t lineEnd = text.find('\n', match);
    return text.substr(match, lineEnd == std::string::npos ? std::string::npos : lineEnd - match);
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
    JsonValue value;
    std::string error;
    Require(
        JsonValue::Parse(R"({"a": [1, -2.5, "x\nžż"], "b": {"c": true, "d": null}})", value, error),
        "MiniJson did not parse a valid document");
    Require(value.GetKind() == JsonValue::Kind::Object, "MiniJson root kind is wrong");
    const JsonValue* array = value.Find("a");
    Require(array != nullptr && array->Size() == 3U, "MiniJson array is wrong");
    Require(array->At(0U)->AsNumber() == 1.0, "MiniJson number is wrong");
    Require(array->At(1U)->AsNumber() == -2.5, "MiniJson negative number is wrong");
    Require(array->At(2U)->AsString() == "x\n\xC5\xBE\xC5\xBC", "MiniJson string escapes are wrong");
    const JsonValue* nested = value.Find("b");
    Require(nested != nullptr && nested->Find("c")->AsBool(), "MiniJson nested bool is wrong");
    Require(nested->Find("d")->IsNull(), "MiniJson null is wrong");

    const std::string dumped = value.Dump();
    JsonValue reparsed;
    Require(JsonValue::Parse(dumped, reparsed, error), "MiniJson dump did not round-trip");
    Require(reparsed.Find("a")->Size() == 3U, "MiniJson round-trip lost data");

    Require(!JsonValue::Parse("{\"a\":1,}", value, error), "MiniJson accepted a trailing comma document");
    Require(!JsonValue::Parse("[1] junk", value, error), "MiniJson accepted trailing characters");
    Require(!JsonValue::Parse("\"\\q\"", value, error), "MiniJson accepted an invalid escape");
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
    Require(!Contains(run.output, "PlayerMoved"), "player controller template emitted movement without input");
    Require(Contains(run.output, "0 diagnostics"), "player controller template run was not clean");
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
    constexpr std::string_view kPhysicsLayersVirtualPath = "/Game/Config/Runtime.21kbphysicslayers";
    kb::scene::PhysicsLayersAsset physicsLayers;
    physicsLayers.layerNames[1] = "Ghost";
    physicsLayers.layerNames[2] = "Solid";
    physicsLayers.layerNames[3] = "Query";
    physicsLayers.SetLayersInteract(1U, 2U, false);
    std::error_code physicsLayersDirectoryError;
    std::filesystem::create_directories(root / "Assets" / "Config", physicsLayersDirectoryError);
    Require(!physicsLayersDirectoryError, "LIB-129 CLI runtime physics layers directory could not be created");
    Require(
        kb::scene::WritePhysicsLayersAsset(root / "Assets" / "Config" / "Runtime.21kbphysicslayers", physicsLayers),
        "LIB-129 CLI runtime physics layers asset could not be written");
    WriteProjectDescriptor(root, true, std::string{ kPhysicsLayersVirtualPath });

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
local lib129Completed = false
local lib130Completed = false
local characterGravityObserved = false
local characterGroundingLanded = false
local characterJumpRequested = false
local characterJumpObserved = false
local platformTargetX = 320.0
local lib131Completed = false
local fastMoverReported = false
local parentedBodyReported = false
local dynamicParentedBodyReported = false
local parentedSampleSeconds = 0.0
local parentedSampleReported = false
local colliderLifecyclePhase = 0
local colliderLifecycleEntity = 0
local colliderLifecycleReported = false
local unloadSceneId = 0
local unloadAnchor = 0
local unloadJointed = 0
local unloadCharacter = 0
local unloadPhase = 0
local sceneUnloadReported = false
local lib133Completed = false
local determinismFixedTickCount = 0
local determinismTickCount = 0
local determinismMutationReported = false
local determinismPhaseProbeMutationApplied = false
local determinismSameStepReported = false
local lib134Completed = false

)" R"(
function FixedTick(self, dt)
    determinismFixedTickCount = determinismFixedTickCount + 1
    if determinismFixedTickCount == 30 then
        local body1 = World.FindByName("DeterminismRuntimeBody1")
        local body2 = World.FindByName("DeterminismRuntimeBody2")
        local body3 = World.FindByName("DeterminismRuntimeBody3")
        local body4 = World.FindByName("DeterminismRuntimeBody4")
        local phaseProbe = World.FindByName("DeterminismPhaseProbeRuntime")
        local applied1 = Physics.AddImpulse(body1, 0.75, 0.20, -0.10)
        local applied2 = Physics.AddImpulse(body2, -0.35, 0.10, 0.25)
        local applied3 = Physics.AddImpulse(body3, 0.20, 0.30, 0.15)
        local applied4 = Physics.AddImpulse(body4, -0.15, 0.25, -0.20)
        determinismPhaseProbeMutationApplied = Physics.SetVelocity(phaseProbe, 12.0, 0.0, 0.0)
        if applied1 and applied2 and applied3 and applied4
            and determinismPhaseProbeMutationApplied then
            determinismMutationReported = true
            Log("physics runtime determinism fixed mutation applied")
        end
    end
end

function Tick(self, dt)
    determinismTickCount = determinismTickCount + 1
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
    local pointJointed = World.FindByName("PointJointedRuntime")
    local distanceJointed = World.FindByName("DistanceJointedRuntime")
    local hingeLimited = World.FindByName("HingeLimitedRuntime")
    local hingeFree = World.FindByName("HingeFreeRuntime")
    local shallowSlopeCharacter = World.FindByName("ShallowSlopeCharacterRuntime")
    local steepSlopeCharacter = World.FindByName("SteepSlopeCharacterRuntime")
    local walkableStepCharacter = World.FindByName("WalkableStepCharacterRuntime")
    local blockedStepCharacter = World.FindByName("BlockedStepCharacterRuntime")
    local groundingCharacter = World.FindByName("GroundingCharacterRuntime")
    local movingPlatform = World.FindByName("MovingPlatformRuntime")
    local platformCharacter = World.FindByName("PlatformRidingCharacterRuntime")
    local tunnelingSphere = World.FindByName("TunnelingSphereRuntime")
    local arrestedSphere = World.FindByName("ArrestedSphereRuntime")
    local parentedBody = World.FindByName("ParentedRigidbodyChildRuntime")
    local dynamicParentedParent = World.FindByName("DynamicRigidbodyParentRuntime")
    local dynamicParentedChild = World.FindByName("DynamicRigidbodyChildRuntime")
    local unloadSurvivor = World.FindByName("SceneUnloadSurvivorRuntime")

    if determinismFixedTickCount == 30 and determinismPhaseProbeMutationApplied
        and not determinismSameStepReported then
        local phaseProbePosition = Transform.GetPosition(
            World.FindByName("DeterminismPhaseProbeRuntime"))
        if phaseProbePosition ~= nil and phaseProbePosition.x > 1450.1 then
            determinismSameStepReported = true
            Log("physics runtime determinism fixed mutation simulated same step")
        end
    end

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

)" R"(
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
        local queryLayer = Physics.LayerBit("Query")
        local solidLayer = Physics.LayerBit("Solid")
        local ray = Physics.Raycast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, queryLayer)
        local sphere = Physics.SphereCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, queryLayer)
        local box = Physics.BoxCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.2, 0.2, queryLayer)
        local capsule = Physics.CapsuleCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.8, queryLayer)
        local overlapSphere = Physics.OverlapSphere(15.0, 0.0, 0.0, 0.75, queryLayer)
        local overlapBox = Physics.OverlapBox(15.0, 0.0, 0.0, 0.75, 0.75, 0.75, queryLayer)
        local overlapCapsule = Physics.OverlapCapsule(15.0, 0.0, 0.0, 0.4, 1.5, queryLayer)
        local closest = Physics.ClosestPoint(queryTarget, 15.0, 3.0, 0.0, queryLayer)
        -- Every query must reject the same real body when its layer is not
        -- selected. This is intentionally not an empty-world test: mask 4
        -- differs from QueryTarget's serialized layer 8 while all geometry
        -- remains identical to the positive calls above.
        local maskedRay = Physics.Raycast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, solidLayer)
        local maskedSphere = Physics.SphereCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, solidLayer)
        local maskedBox = Physics.BoxCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.2, 0.2, solidLayer)
        local maskedCapsule = Physics.CapsuleCast(12.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.2, 0.8, solidLayer)
        local maskedOverlapSphere = Physics.OverlapSphere(15.0, 0.0, 0.0, 0.75, solidLayer)
        local maskedOverlapBox = Physics.OverlapBox(15.0, 0.0, 0.0, 0.75, 0.75, 0.75, solidLayer)
        local maskedOverlapCapsule = Physics.OverlapCapsule(15.0, 0.0, 0.0, 0.4, 1.5, solidLayer)
        local maskedClosest = Physics.ClosestPoint(queryTarget, 15.0, 3.0, 0.0, solidLayer)
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
            -- Every single-result production query above now consumes the
            -- same capacity-one caller buffer path as LIB-126's native
            -- all-hit API. The multi-hit/capacity ordering contract is
            -- covered against the same Jolt scene by PhysicsSceneSystemTests.
            Log("physics runtime LIB-126 complete")
        end
    end

    if not lib123Completed and characterReported and jointReported then
        lib123Completed = true
        Log("physics runtime LIB-123 complete")
    end

    -- Production proof for every advertised JointType. These components
    -- came from the serialized scene and reached the dynamically loaded
    -- Jolt plugin through the normal fixed-update path.
    if not lib130Completed and jointReported then
        local pointPosition = Transform.GetPosition(pointJointed)
        local distancePosition = Transform.GetPosition(distanceJointed)
        local limitedVelocity = Physics.GetAngularVelocity(hingeLimited)
        local freeVelocity = Physics.GetAngularVelocity(hingeFree)
        if pointPosition ~= nil and math.abs(pointPosition.y - 10.0) < 0.1
            and distancePosition ~= nil
            and (10.0 - distancePosition.y) > 2.5
            and (10.0 - distancePosition.y) < 3.3
            and limitedVelocity.found and freeVelocity.found
            and math.abs(freeVelocity.y) > 5.0
            and math.abs(limitedVelocity.y) < math.abs(freeVelocity.y) * 0.5 then
            lib130Completed = true
            Log("physics runtime LIB-130 complete")
        end
    end

)" R"(
    -- Drive the two step rigs and the kinematic platform through public
    -- production APIs every frame. Calls made before Jolt creates the live
    -- objects fail honestly and are retried on the following frame.
    Physics.CharacterMove(walkableStepCharacter, 2.0, 0.0)
    Physics.CharacterMove(blockedStepCharacter, 2.0, 0.0)
    platformTargetX = platformTargetX + dt
    Physics.MoveKinematic(movingPlatform, platformTargetX, -0.5, 0.0)

    local groundingPosition = Transform.GetPosition(groundingCharacter)
    local groundingVelocity = Physics.CharacterVelocity(groundingCharacter)
    if not characterGravityObserved and groundingPosition ~= nil
        and groundingPosition.y > 1.5
        and groundingVelocity.found and groundingVelocity.y < -1.0
        and not Physics.CharacterIsGrounded(groundingCharacter) then
        characterGravityObserved = true
        Log("physics runtime character gravity observed")
    end

    if characterGravityObserved and not characterGroundingLanded
        and groundingPosition ~= nil
        and groundingPosition.y > 0.7 and groundingPosition.y < 1.4
        and Physics.CharacterIsGrounded(groundingCharacter) then
        characterGroundingLanded = true
        Log("physics runtime character grounded")
    end

    if characterGroundingLanded and not characterJumpRequested
        and Physics.CharacterJump(groundingCharacter, 5.0) then
        characterJumpRequested = true
        Log("physics runtime character jump requested")
    end

    if characterJumpRequested and not characterJumpObserved then
        local jumpVelocity = Physics.CharacterVelocity(groundingCharacter)
        if jumpVelocity.found and jumpVelocity.y > 3.0
            and not Physics.CharacterIsGrounded(groundingCharacter) then
            characterJumpObserved = true
            Log("physics runtime character jump observed")
        end
    end

    if not lib131Completed and characterGravityObserved and characterGroundingLanded
        and characterJumpObserved then
        local shallowPosition = Transform.GetPosition(shallowSlopeCharacter)
        local steepPosition = Transform.GetPosition(steepSlopeCharacter)
        local shallowNormal = Physics.CharacterGroundNormal(shallowSlopeCharacter)
        local walkablePosition = Transform.GetPosition(walkableStepCharacter)
        local blockedPosition = Transform.GetPosition(blockedStepCharacter)
        local platformPosition = Transform.GetPosition(movingPlatform)
        local platformCharacterPosition = Transform.GetPosition(platformCharacter)
        local platformGroundVelocity = Physics.CharacterGroundVelocity(platformCharacter)
        if shallowPosition ~= nil and steepPosition ~= nil
            and Physics.CharacterIsGrounded(shallowSlopeCharacter)
            and shallowNormal.found and shallowNormal.y > 0.85
            and steepPosition.y < shallowPosition.y - 5.0
            and walkablePosition ~= nil and walkablePosition.x > 165.0 and walkablePosition.y > 1.0
            and blockedPosition ~= nil and blockedPosition.x < 225.0 and blockedPosition.y < 1.0
            and platformPosition ~= nil and platformCharacterPosition ~= nil
            and (platformPosition.x - 320.0) > 3.0
            and math.abs((platformCharacterPosition.x - 320.0) - (platformPosition.x - 320.0)) < 1.0
            and Physics.CharacterIsGrounded(platformCharacter)
            and platformGroundVelocity.found and platformGroundVelocity.x > 0.5 then
            lib131Completed = true
            Log("physics runtime LIB-131 complete")
        end
    end

)" R"(
    local tunnelingPosition = Transform.GetPosition(tunnelingSphere)
    local arrestedPosition = Transform.GetPosition(arrestedSphere)
    if not fastMoverReported and tunnelingPosition ~= nil and arrestedPosition ~= nil
        and tunnelingPosition.x > 1001.0 and arrestedPosition.x < 1000.0 then
        fastMoverReported = true
        Log("physics runtime fast mover CCD verified")
    end

    local parentedPosition = Transform.GetPosition(parentedBody)
    parentedSampleSeconds = parentedSampleSeconds + dt
    if not parentedSampleReported and parentedSampleSeconds > 4.0 and parentedPosition ~= nil then
        parentedSampleReported = true
        Log("physics runtime parented sample x=" .. parentedPosition.x .. " y=" .. parentedPosition.y)
    end
    if not parentedBodyReported and parentedPosition ~= nil
        and parentedPosition.x > 1.0 and parentedPosition.x < 3.0
        and parentedPosition.y > 0.0 and parentedPosition.y < 1.0 then
        parentedBodyReported = true
        Log("physics runtime parented rigidbody verified")
    end

    local dynamicParentPosition = Transform.GetPosition(dynamicParentedParent)
    local dynamicChildPosition = Transform.GetPosition(dynamicParentedChild)
    local dynamicParentVelocity = Physics.GetVelocity(dynamicParentedParent)
    local dynamicChildVelocity = Physics.GetVelocity(dynamicParentedChild)
    if not dynamicParentedBodyReported and parentedSampleSeconds > 4.0
        and dynamicParentPosition ~= nil and dynamicParentPosition.x > 824.0
        and dynamicChildPosition ~= nil
        and dynamicChildPosition.x > 2.99 and dynamicChildPosition.x < 3.01
        and dynamicParentVelocity.found and dynamicParentVelocity.x > 1.0
        and dynamicChildVelocity.found and dynamicChildVelocity.x > 1.0 then
        dynamicParentedBodyReported = true
        Log("physics runtime dynamic parented rigidbody verified")
    end

    if colliderLifecyclePhase == 0 then
        colliderLifecycleEntity = World.InstantiatePrefab({
            prefab = "/Game/Prefabs/LifecycleCollider.kbprefab",
            x = 1050.0,
            y = 20.0,
            z = 0.0
        })
        if colliderLifecycleEntity ~= nil and colliderLifecycleEntity ~= 0 then
            colliderLifecyclePhase = 1
        end
    elseif colliderLifecyclePhase == 1 then
        local position = Transform.GetPosition(colliderLifecycleEntity)
        local velocity = Physics.GetVelocity(colliderLifecycleEntity)
        if position ~= nil and position.y < 19.5 and velocity.found
            and World.Destroy(colliderLifecycleEntity) then
            colliderLifecyclePhase = 2
        end
    elseif colliderLifecyclePhase == 2 then
        local velocity = Physics.GetVelocity(colliderLifecycleEntity)
        if not World.Exists(colliderLifecycleEntity) and not velocity.found then
            colliderLifecycleEntity = World.InstantiatePrefab({
                prefab = "/Game/Prefabs/LifecycleCollider.kbprefab",
                x = 1050.0,
                y = 20.0,
                z = 0.0
            })
            if colliderLifecycleEntity ~= nil and colliderLifecycleEntity ~= 0 then
                colliderLifecyclePhase = 3
            end
        end
    elseif colliderLifecyclePhase == 3 and not colliderLifecycleReported then
        local position = Transform.GetPosition(colliderLifecycleEntity)
        local velocity = Physics.GetVelocity(colliderLifecycleEntity)
        if position ~= nil and position.y < 19.5 and velocity.found then
            colliderLifecycleReported = true
            Log("physics runtime collider spawn despawn respawn verified")
        end
    end

    if unloadPhase == 0 then
        unloadSceneId = Scene.Load("/Game/Scenes/UnloadPhysics.21kbscene", true)
        if unloadSceneId ~= nil and unloadSceneId ~= 0
            and Scene.LoadProgress(unloadSceneId) == 1.0
            and Scene.Find("UnloadPhysics") == unloadSceneId then
            unloadPhase = 1
            Log("physics runtime additive physics scene loaded")
        end
    elseif unloadPhase == 1 then
        unloadAnchor = World.FindByName("UnloadAnchorRuntime")
        unloadJointed = World.FindByName("UnloadJointedRuntime")
        unloadCharacter = World.FindByName("UnloadCharacterRuntime")
        local anchorVelocity = Physics.GetVelocity(unloadAnchor)
        local jointedVelocity = Physics.GetVelocity(unloadJointed)
        local characterVelocity = Physics.CharacterVelocity(unloadCharacter)
        if anchorVelocity.found and jointedVelocity.found and characterVelocity.found
            and Scene.Unload(unloadSceneId) then
            unloadPhase = 2
            Log("physics runtime physics scene unload requested")
        end
    elseif unloadPhase == 2 and not sceneUnloadReported then
        local staleAnchorVelocity = Physics.GetVelocity(unloadAnchor)
        local staleJointedVelocity = Physics.GetVelocity(unloadJointed)
        local staleCharacterVelocity = Physics.CharacterVelocity(unloadCharacter)
        if Scene.LoadProgress(unloadSceneId) == 0.0
            and Scene.Find("UnloadPhysics") == 0
            and not World.Exists(unloadAnchor) and not World.Exists(unloadJointed)
            and not World.Exists(unloadCharacter)
            and not staleAnchorVelocity.found and not staleJointedVelocity.found
            and not staleCharacterVelocity.found then
            sceneUnloadReported = true
            Log("physics runtime physics scene unloaded")
        end
    end

    if not lib133Completed and fastMoverReported and parentedBodyReported
        and dynamicParentedBodyReported
        and colliderLifecycleReported and sceneUnloadReported then
        local survivorPosition = Transform.GetPosition(unloadSurvivor)
        local survivorVelocity = Physics.GetVelocity(unloadSurvivor)
        if survivorPosition ~= nil and survivorPosition.x > 1101.0
            and survivorVelocity.found and survivorVelocity.x > 1.5 then
            lib133Completed = true
            Log("physics runtime LIB-133 complete")
        end
    end

    if not lib134Completed and determinismTickCount >= 90
        and determinismMutationReported and determinismSameStepReported then
        local signature = ""
        local signatureValid = true
        for index = 1, 4 do
            local entity = World.FindByName("DeterminismRuntimeBody" .. index)
            local position = Transform.GetPosition(entity)
            local velocity = Physics.GetVelocity(entity)
            local angularVelocity = Physics.GetAngularVelocity(entity)
            if position == nil or not velocity.found or not angularVelocity.found then
                signatureValid = false
            else
                signature = signature .. string.format(
                    "|%a,%a,%a,%a,%a,%a,%a,%a,%a",
                    position.x, position.y, position.z,
                    velocity.x, velocity.y, velocity.z,
                    angularVelocity.x, angularVelocity.y, angularVelocity.z)
            end
        end
        if signatureValid then
            lib134Completed = true
            Log("physics runtime determinism signature " .. signature)
            Log("physics runtime LIB-134 complete")
        end
    end

    if not lib129Completed then
        local ghostLayer = Physics.LayerBit("Ghost")
        local solidLayer = Physics.LayerBit("Solid")
        local queryLayer = Physics.LayerBit("Query")
        local disabledStatic = Transform.GetPosition(World.FindByName("MatrixDisabledStatic"))
        local disabledDynamic = Transform.GetPosition(World.FindByName("MatrixDisabledDynamic"))
        local controlStatic = Transform.GetPosition(World.FindByName("MatrixControlStatic"))
        local controlDynamic = Transform.GetPosition(World.FindByName("MatrixControlDynamic"))
        if ghostLayer == 2 and solidLayer == 4 and queryLayer == 8
            and disabledStatic ~= nil and disabledDynamic ~= nil
            and controlStatic ~= nil and controlDynamic ~= nil
            and math.abs(disabledDynamic.x - disabledStatic.x) < 0.3
            and math.abs(controlDynamic.x - controlStatic.x) > 0.7 then
            lib129Completed = true
            Log("physics runtime LIB-129 complete")
        end
    end
end
)");
    WriteTextFile(root / "Assets" / "Logic" / "PhysicsContactProbe.lua", R"(
local triggerEnter = false
local triggerStay = false
local triggerExit = false
local collisionEnter = false
local collisionStay = false
local collisionExit = false
local launched = false
local completed = false

local function validPayload(event)
    if event == nil or event.args == nil or event.args.other == nil or event.args.other == 0 then
        return false
    end
    local nx = event.args.normalX
    local ny = event.args.normalY
    local nz = event.args.normalZ
    local px = event.args.pointX
    local py = event.args.pointY
    local pz = event.args.pointZ
    if nx == nil or ny == nil or nz == nil or px == nil or py == nil or pz == nil then
        return false
    end
    return (nx * nx + ny * ny + nz * nz) > 0.25
end

local function reportComplete()
    if not completed and triggerEnter and triggerStay and triggerExit
        and collisionEnter and collisionStay and collisionExit then
        completed = true
        Log("physics runtime LIB-127 complete")
    end
end

function OnTriggerEnter(self, event)
    if validPayload(event) then
        triggerEnter = true
        Log("physics runtime OnTriggerEnter")
    end
    reportComplete()
end

function OnTriggerStay(self, event)
    if validPayload(event) then
        triggerStay = true
        Log("physics runtime OnTriggerStay")
    end
    reportComplete()
end

function OnTriggerExit(self, event)
    if validPayload(event) then
        triggerExit = true
        Log("physics runtime OnTriggerExit")
    end
    reportComplete()
end

function OnCollisionEnter(self, event)
    if validPayload(event) then
        collisionEnter = true
        Log("physics runtime OnCollisionEnter")
    end
    reportComplete()
end

function OnCollisionStay(self, event)
    if validPayload(event) then
        collisionStay = true
        Log("physics runtime OnCollisionStay")
        if not launched and Physics.AddImpulse(self.entity, 0.0, 8.0, 0.0) then
            launched = true
        end
    end
    reportComplete()
end

function OnCollisionExit(self, event)
    -- This explicitly validates the retained final manifold payload:
    -- Jolt's removal callback itself has no point/normal.
    if validPayload(event) then
        collisionExit = true
        Log("physics runtime OnCollisionExit")
    end
    reportComplete()
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

    const auto createMatrixBody = [&scene](
                                      const char* name,
                                      kb::scene::RigidbodyBodyType bodyType,
                                      float x,
                                      std::uint32_t layer) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ x, 0.0F, 20.0F },
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
                .radius = 0.5F,
                .layer = layer,
            });
    };
    createMatrixBody("MatrixDisabledStatic", kb::scene::RigidbodyBodyType::Static, 100.0F, 4U);
    createMatrixBody("MatrixDisabledDynamic", kb::scene::RigidbodyBodyType::Dynamic, 100.2F, 2U);
    createMatrixBody("MatrixControlStatic", kb::scene::RigidbodyBodyType::Static, 110.0F, 1U);
    createMatrixBody("MatrixControlDynamic", kb::scene::RigidbodyBodyType::Dynamic, 110.2F, 1U);

    const kb::scene::SceneObject contactFloor = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "ContactFloor",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 60.0F, -0.5F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        contactFloor.Entity(),
        kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Static });
    scene.Components().Colliders().Set(
        contactFloor.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{ 6.0F, 1.0F, 6.0F },
        });

    const kb::scene::SceneObject contactTrigger = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "ContactTrigger",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 60.0F, 3.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        contactTrigger.Entity(),
        kb::scene::RigidbodyComponent{ .bodyType = kb::scene::RigidbodyBodyType::Static });
    scene.Components().Colliders().Set(
        contactTrigger.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Box,
            .boxSize = kb::scene::Vec3{ 2.0F, 2.0F, 2.0F },
            .trigger = true,
        });

    const kb::scene::SceneObject contactFaller = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "ContactFaller",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 60.0F, 8.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        contactFaller.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
        });
    scene.Components().Colliders().Set(
        contactFaller.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.5F,
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

    const auto createWorldJointBody = [&scene](
                                          const char* name,
                                          kb::scene::Vec3 position,
                                          kb::scene::Vec3 angularVelocity,
                                          bool useGravity) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{ .localPosition = position },
            });
        scene.Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 1.0F,
                .angularVelocity = angularVelocity,
                .useGravity = useGravity,
            });
        scene.Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.3F,
            });
        return object;
    };

    const kb::scene::Vec3 pointStart{ 70.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject pointJointed =
        createWorldJointBody("PointJointedRuntime", pointStart, {}, true);
    scene.Components().Joints().Set(
        pointJointed.Entity(),
        kb::scene::JointComponent{
            .type = kb::scene::JointType::Point,
            .connectedEntity = {},
            .anchor = {},
            .connectedAnchor = pointStart,
        });

    const kb::scene::Vec3 distanceStart{ 74.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject distanceJointed =
        createWorldJointBody("DistanceJointedRuntime", distanceStart, {}, true);
    scene.Components().Joints().Set(
        distanceJointed.Entity(),
        kb::scene::JointComponent{
            .type = kb::scene::JointType::Distance,
            .connectedEntity = {},
            .anchor = {},
            .connectedAnchor = distanceStart,
            .minLimit = 0.0F,
            .maxLimit = 3.0F,
            .enableLimit = true,
        });

    constexpr kb::scene::Vec3 hingeAxis{ 0.0F, 1.0F, 0.0F };
    constexpr kb::scene::Vec3 hingeSpin{ 0.0F, 10.0F, 0.0F };
    const kb::scene::Vec3 hingeLimitedStart{ 78.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject hingeLimited =
        createWorldJointBody("HingeLimitedRuntime", hingeLimitedStart, hingeSpin, false);
    scene.Components().Joints().Set(
        hingeLimited.Entity(),
        kb::scene::JointComponent{
            .type = kb::scene::JointType::Hinge,
            .connectedEntity = {},
            .anchor = {},
            .connectedAnchor = hingeLimitedStart,
            .axis = hingeAxis,
            .minLimit = -5.0F,
            .maxLimit = 5.0F,
            .enableLimit = true,
        });

    const kb::scene::Vec3 hingeFreeStart{ 82.0F, 10.0F, 0.0F };
    const kb::scene::SceneObject hingeFree =
        createWorldJointBody("HingeFreeRuntime", hingeFreeStart, hingeSpin, false);
    scene.Components().Joints().Set(
        hingeFree.Entity(),
        kb::scene::JointComponent{
            .type = kb::scene::JointType::Hinge,
            .connectedEntity = {},
            .anchor = {},
            .connectedAnchor = hingeFreeStart,
            .axis = hingeAxis,
            .enableLimit = false,
        });

    const auto createCharacterRigBody = [&scene](
                                            const char* name,
                                            kb::scene::Vec3 position,
                                            kb::scene::RigidbodyBodyType bodyType,
                                            kb::scene::Vec3 size,
                                            kb::scene::Quat rotation = {}) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = position,
                    .localRotation = rotation,
                },
            });
        scene.Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{ .bodyType = bodyType });
        scene.Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Box,
                .boxSize = size,
            });
        return object;
    };
    const auto createCharacter = [&scene](
                                     const char* name,
                                     kb::scene::Vec3 position,
                                     kb::scene::CharacterControllerComponent component) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{ .localPosition = position },
            });
        scene.Components().CharacterControllers().Set(object.Entity(), component);
        return object;
    };
    const kb::scene::CharacterControllerComponent defaultCharacter{
        .radius = 0.4F,
        .height = 1.8F,
    };
    constexpr float characterHalfHeight = 0.9F;

    createCharacterRigBody(
        "SlopeCatchFloorRuntime",
        kb::scene::Vec3{ 110.0F, -20.0F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 60.0F, 2.0F, 30.0F });
    createCharacterRigBody(
        "ShallowRampRuntime",
        kb::scene::Vec3{ 100.0F, 0.0F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 6.0F, 0.5F, 6.0F },
        kb::scene::Quat{ 0.0F, 0.0F, 0.173648F, 0.984808F });
    createCharacterRigBody(
        "SteepRampRuntime",
        kb::scene::Vec3{ 120.0F, 0.0F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 6.0F, 0.5F, 6.0F },
        kb::scene::Quat{ 0.0F, 0.0F, 0.608761F, 0.793353F });
    createCharacter(
        "ShallowSlopeCharacterRuntime",
        kb::scene::Vec3{ 100.0F, 5.0F, 0.0F },
        defaultCharacter);
    createCharacter(
        "SteepSlopeCharacterRuntime",
        kb::scene::Vec3{ 120.0F, 5.0F, 0.0F },
        defaultCharacter);

    createCharacterRigBody(
        "WalkableStepFloorRuntime",
        kb::scene::Vec3{ 160.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 8.0F, 1.0F, 6.0F });
    createCharacterRigBody(
        "WalkableStepRuntime",
        kb::scene::Vec3{ 183.0F, -4.7F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 40.0F, 10.0F, 6.0F });
    createCharacter(
        "WalkableStepCharacterRuntime",
        kb::scene::Vec3{ 158.0F, characterHalfHeight, 0.0F },
        kb::scene::CharacterControllerComponent{
            .radius = 0.4F,
            .height = 1.8F,
            .stepOffset = 0.5F,
        });

    createCharacterRigBody(
        "BlockedStepFloorRuntime",
        kb::scene::Vec3{ 220.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 8.0F, 1.0F, 6.0F });
    createCharacterRigBody(
        "BlockedStepRuntime",
        kb::scene::Vec3{ 243.0F, -4.7F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 40.0F, 10.0F, 6.0F });
    createCharacter(
        "BlockedStepCharacterRuntime",
        kb::scene::Vec3{ 218.0F, characterHalfHeight, 0.0F },
        kb::scene::CharacterControllerComponent{
            .radius = 0.4F,
            .height = 1.8F,
            .stepOffset = 0.1F,
        });

    createCharacterRigBody(
        "GroundingFloorRuntime",
        kb::scene::Vec3{ 290.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 8.0F, 1.0F, 8.0F });
    createCharacter(
        "GroundingCharacterRuntime",
        kb::scene::Vec3{ 290.0F, 5.0F, 0.0F },
        defaultCharacter);

    createCharacterRigBody(
        "MovingPlatformRuntime",
        kb::scene::Vec3{ 320.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Kinematic,
        kb::scene::Vec3{ 4.0F, 1.0F, 4.0F });
    createCharacterRigBody(
        "PlatformCatchFloorRuntime",
        kb::scene::Vec3{ 340.0F, -20.0F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 80.0F, 2.0F, 20.0F });
    createCharacter(
        "PlatformRidingCharacterRuntime",
        kb::scene::Vec3{ 320.0F, characterHalfHeight, 0.0F },
        defaultCharacter);

    constexpr float fastMoverWallX = 1000.0F;
    createCharacterRigBody(
        "FastMoverWallRuntime",
        kb::scene::Vec3{ fastMoverWallX, 0.0F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 0.1F, 10.0F, 10.0F });
    const auto createFastMover = [&scene](const char* name, float z, bool continuous) {
        const kb::scene::SceneObject object = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = name,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ fastMoverWallX - 5.0F, 0.0F, z },
                },
            });
        scene.Components().Rigidbodies().Set(
            object.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 1.0F,
                .linearVelocity = kb::scene::Vec3{ 200.0F, 0.0F, 0.0F },
                .useGravity = false,
                .useContinuousCollision = continuous,
            });
        scene.Components().Colliders().Set(
            object.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.2F,
            });
    };
    createFastMover("TunnelingSphereRuntime", -3.0F, false);
    createFastMover("ArrestedSphereRuntime", 3.0F, true);

    const kb::scene::SceneObject parentedRigParent = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "ParentedRigidbodyParentRuntime",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 700.0F, 0.0F, 0.0F },
                .localRotation = kb::scene::Quat{ 0.0F, 1.0F, 0.0F, 0.0F },
            },
        });
    createCharacterRigBody(
        "ParentedRigidbodyFloorRuntime",
        kb::scene::Vec3{ 700.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 20.0F, 1.0F, 20.0F });
    const kb::scene::SceneObject parentedRigChild = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "ParentedRigidbodyChildRuntime",
            .parent = parentedRigParent,
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 2.0F, 5.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        parentedRigChild.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
        });
    scene.Components().Colliders().Set(
        parentedRigChild.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.3F,
        });

    const kb::scene::SceneObject dynamicRigChild = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "DynamicRigidbodyChildRuntime",
        });
    const kb::scene::SceneObject dynamicRigParent = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "DynamicRigidbodyParentRuntime",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 800.0F, 100.0F, 0.0F },
            },
        });
    Require(
        scene.Hierarchy().SetParent(dynamicRigChild.Entity(), dynamicRigParent.Entity()),
        "LIB-133 runtime dynamic rigidbody hierarchy could not be created");
    kb::scene::TransformComponent dynamicRigChildTransform = scene.Transforms().Get(dynamicRigChild);
    dynamicRigChildTransform.localPosition = kb::scene::Vec3{ 3.0F, 0.0F, 0.0F };
    scene.Transforms().Set(dynamicRigChild.Entity(), dynamicRigChildTransform);
    scene.Components().Rigidbodies().Set(
        dynamicRigChild.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
            .linearVelocity = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F },
            .useGravity = false,
        });
    scene.Components().Colliders().Set(
        dynamicRigChild.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.25F,
        });
    scene.Components().Rigidbodies().Set(
        dynamicRigParent.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
            .linearVelocity = kb::scene::Vec3{ 6.0F, 0.0F, 0.0F },
            .useGravity = false,
        });
    scene.Components().Colliders().Set(
        dynamicRigParent.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.25F,
        });

    // LIB-134 production determinism rig. It is serialized into the same project scene as
    // the rest of the player probe, then mutated from Lua FixedTick in a fixed entity/call
    // order. The exact-state signature is compared only between replays of this same binary
    // and configuration; CROSS_PLATFORM_DETERMINISTIC is intentionally not claimed.
    createCharacterRigBody(
        "DeterminismRuntimeFloor",
        kb::scene::Vec3{ 1400.0F, -0.5F, 0.0F },
        kb::scene::RigidbodyBodyType::Static,
        kb::scene::Vec3{ 10.0F, 1.0F, 10.0F });
    constexpr std::array<kb::scene::Vec3, 4> determinismPositions{ {
        { 1400.0F, 1.0F, 0.0F },
        { 1400.2F, 2.2F, 0.1F },
        { 1399.8F, 3.4F, -0.1F },
        { 1400.1F, 4.6F, 0.2F },
    } };
    for (std::size_t index = 0; index < determinismPositions.size(); ++index) {
        const kb::scene::SceneObject body = scene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = "DeterminismRuntimeBody" + std::to_string(index + 1U),
                .transform = kb::scene::TransformComponent{
                    .localPosition = determinismPositions[index],
                },
            });
        scene.Components().Rigidbodies().Set(
            body.Entity(),
            kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 1.0F,
                .linearVelocity = kb::scene::Vec3{
                    0.1F * static_cast<float>(index + 1U),
                    0.0F,
                    -0.05F * static_cast<float>(index),
                },
                .angularVelocity = kb::scene::Vec3{
                    0.3F + 0.1F * static_cast<float>(index),
                    -0.2F + 0.05F * static_cast<float>(index),
                    0.15F * static_cast<float>(index + 1U),
                },
            });
        scene.Components().Colliders().Set(
            body.Entity(),
            kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Box,
                .boxSize = kb::scene::Vec3{ 0.8F, 0.8F, 0.8F },
                .friction = 0.6F,
                .restitution = 0.15F,
            });
    }
    const kb::scene::SceneObject determinismPhaseProbe = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "DeterminismPhaseProbeRuntime",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1450.0F, 20.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        determinismPhaseProbe.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
            .useGravity = false,
        });
    scene.Components().Colliders().Set(
        determinismPhaseProbe.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.25F,
        });

    const kb::scene::SceneObject unloadSurvivor = scene.Entities().CreateObject(
        kb::scene::SceneObjectDesc{
            .name = "SceneUnloadSurvivorRuntime",
            .transform = kb::scene::TransformComponent{
                .localPosition = kb::scene::Vec3{ 1100.0F, 40.0F, 0.0F },
            },
        });
    scene.Components().Rigidbodies().Set(
        unloadSurvivor.Entity(),
        kb::scene::RigidbodyComponent{
            .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
            .mass = 1.0F,
            .linearVelocity = kb::scene::Vec3{ 2.0F, 0.0F, 0.0F },
            .useGravity = false,
        });
    scene.Components().Colliders().Set(
        unloadSurvivor.Entity(),
        kb::scene::ColliderComponent{
            .shape = kb::scene::ColliderShape::Sphere,
            .radius = 0.3F,
        });

    std::error_code lifecycleDirectoryError;
    std::filesystem::create_directories(root / "Assets" / "Prefabs", lifecycleDirectoryError);
    std::filesystem::create_directories(root / "Assets" / "Scenes", lifecycleDirectoryError);
    Require(!lifecycleDirectoryError, "LIB-133 runtime lifecycle asset directories could not be created");

    kb::scene::ScenePrefab lifecyclePrefab;
    static_cast<void>(lifecyclePrefab.AddNode(kb::scene::ScenePrefabNodeDesc{
        .name = "LifecycleColliderRuntime",
        .transform = kb::scene::TransformComponent{},
        .components = kb::scene::ScenePrefabNodeComponents{
            .rigidbody = kb::scene::RigidbodyComponent{
                .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                .mass = 1.0F,
            },
            .collider = kb::scene::ColliderComponent{
                .shape = kb::scene::ColliderShape::Sphere,
                .radius = 0.3F,
            },
        },
    }));
    const kb::scene::ScenePrefabHandle lifecyclePrefabHandle =
        scene.Prefabs().Register("LifecycleCollider", std::move(lifecyclePrefab));
    Require(
        lifecyclePrefabHandle.IsValid() &&
            scene.Prefabs().Save(lifecyclePrefabHandle, root / "Assets" / "Prefabs" / "LifecycleCollider.kbprefab"),
        "LIB-133 runtime collider lifecycle prefab could not be saved");

    {
        kb::scene::Scene unloadScene;
        const kb::scene::SceneObject unloadRoot = unloadScene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = "UnloadPhysicsRootRuntime",
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 1150.0F, 0.0F, 0.0F },
                },
            });
        const auto createUnloadBody = [&unloadScene, unloadRoot](
                                          const char* name,
                                          float localX) {
            const kb::scene::SceneObject object = unloadScene.Entities().CreateObject(
                kb::scene::SceneObjectDesc{
                    .name = name,
                    .parent = unloadRoot,
                    .transform = kb::scene::TransformComponent{
                        .localPosition = kb::scene::Vec3{ localX, 20.0F, 0.0F },
                    },
                });
            unloadScene.Components().Rigidbodies().Set(
                object.Entity(),
                kb::scene::RigidbodyComponent{
                    .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                    .mass = 1.0F,
                    .useGravity = false,
                });
            unloadScene.Components().Colliders().Set(
                object.Entity(),
                kb::scene::ColliderComponent{
                    .shape = kb::scene::ColliderShape::Sphere,
                    .radius = 0.3F,
                });
            return object;
        };
        const kb::scene::SceneObject unloadAnchor =
            createUnloadBody("UnloadAnchorRuntime", 0.0F);
        const kb::scene::SceneObject unloadJointed =
            createUnloadBody("UnloadJointedRuntime", 1.0F);
        unloadScene.Components().Joints().Set(
            unloadJointed.Entity(),
            kb::scene::JointComponent{
                .type = kb::scene::JointType::Fixed,
                .connectedEntity = unloadAnchor.Entity(),
                .anchor = {},
                .connectedAnchor = kb::scene::Vec3{ 1.0F, 0.0F, 0.0F },
            });
        const kb::scene::SceneObject unloadCharacter = unloadScene.Entities().CreateObject(
            kb::scene::SceneObjectDesc{
                .name = "UnloadCharacterRuntime",
                .parent = unloadRoot,
                .transform = kb::scene::TransformComponent{
                    .localPosition = kb::scene::Vec3{ 2.0F, 20.0F, 0.0F },
                },
            });
        unloadScene.Components().CharacterControllers().Set(
            unloadCharacter.Entity(),
            kb::scene::CharacterControllerComponent{
                .radius = 0.4F,
                .height = 1.8F,
                .useGravity = false,
            });
        Require(
            kb::scene::SceneDocumentService::Save(
                unloadScene,
                root / "Assets" / "Scenes" / "UnloadPhysics.21kbscene",
                "UnloadPhysics"),
            "LIB-133 additive physics unload scene could not be saved");
    }

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
    const CommandRun attachContactProbe = Run(&kb::cli::RunSceneAttachCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "ContactFaller",
        "--script", "/Game/Logic/PhysicsContactProbe.lua",
    });
    Require(attachContactProbe.exitCode == 0, "LIB-127 physics contact runtime probe could not attach to the scene");

    const CommandRun run = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "300",
    });
    if (run.exitCode != 0) {
        std::fputs(run.output.c_str(), stderr);
    }
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
    Require(Contains(run.output, "[log] physics runtime LIB-126 complete"),
        "LIB-126 NonAlloc production runtime probe did not complete");
    Require(Contains(run.output, "[log] physics runtime character virtual moved"),
        "Jolt CharacterVirtual did not move and write its pose back in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime joint constrained"),
        "serialized JointComponent did not constrain live Jolt bodies in kb_cli runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-123 complete"),
        "LIB-123 character/joint production runtime probe did not complete");
    Require(Contains(run.output, "[log] physics runtime LIB-130 complete"),
        "LIB-130 all advertised JointTypes did not complete through the production runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-131 complete"),
        "LIB-131 character slope, step, grounding, platform motion, gravity, and jump did not complete through the production runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-133 complete"),
        "LIB-133 fast mover, collider lifecycle, parented rigidbody, and scene unload did not complete through the production runtime");
    Require(Contains(run.output, "[log] physics runtime dynamic parented rigidbody verified"),
        "LIB-133 dynamic rigidbody parent+child did not preserve a same-step parent-relative local pose in production runtime");
    Require(Contains(run.output, "[log] physics runtime LIB-129 complete"),
        "LIB-129 project asset did not configure named query layers and the live Jolt collision matrix in kb_cli runtime");
    for (const char* eventName : { "OnTriggerEnter", "OnTriggerStay", "OnTriggerExit", "OnCollisionEnter", "OnCollisionStay", "OnCollisionExit" }) {
        Require(
            Contains(run.output, std::string{ "[log] physics runtime " } + eventName),
            (std::string{ "LIB-127 production runtime did not dispatch " } + eventName + " with a valid payload").c_str());
    }
    Require(Contains(run.output, "[log] physics runtime LIB-127 complete"),
        "LIB-127 full trigger/collision lifecycle did not complete in the serialized kb_cli Jolt runtime");
    Require(Contains(run.output, "[log] physics runtime determinism fixed mutation applied"),
        "LIB-134 production runtime did not apply the ordered FixedTick mutation through live Jolt");
    Require(Contains(run.output, "[log] physics runtime determinism fixed mutation simulated same step"),
        "LIB-134 kb_cli did not run FixedTick in the authoritative pre-simulation phase before the same Jolt step");
    Require(Contains(run.output, "[log] physics runtime LIB-134 complete"),
        "LIB-134 production runtime did not publish a final deterministic physics signature");
    const std::string determinismSignature =
        LineContaining(run.output, "physics runtime determinism signature ");
    Require(!determinismSignature.empty(),
        "LIB-134 first production runtime did not expose its deterministic physics signature");
    Require(Contains(run.output, "0 diagnostics"), "projectile physics runtime was not clean");

    // A second complete player lifetime in the same process must reuse the
    // process-persistent Physics.Jolt library instead of mapping a second
    // shadow copy with a disconnected set of Jolt static globals.
    const CommandRun secondRun = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "300",
    });
    Require(secondRun.exitCode == 0, "second sequential kb_cli runtime reported diagnostics");
    Require(Contains(secondRun.output, "[module] active Physics.Jolt"),
        "second sequential kb_cli runtime did not reactivate the project Physics.Jolt module");
    Require(Contains(secondRun.output, "[log] physics runtime LIB-131 complete"),
        "second sequential kb_cli runtime did not execute live Jolt simulation");
    Require(Contains(secondRun.output, "[log] physics runtime LIB-133 complete"),
        "second sequential kb_cli runtime did not repeat the full LIB-133 physics lifecycle");
    Require(Contains(secondRun.output, "[log] physics runtime LIB-134 complete"),
        "second sequential kb_cli runtime did not repeat the production determinism scenario");
    // Jolt guarantees determinism for the same binary when simulation-mutating calls occur in
    // the same order. This second complete player lifetime is that production replay: project
    // asset load, scheduler-owned FixedTick, dynamic Jolt module, simulation, and write-back.
    const std::string secondDeterminismSignature =
        LineContaining(secondRun.output, "physics runtime determinism signature ");
    Require(!secondDeterminismSignature.empty() && secondDeterminismSignature == determinismSignature,
        "LIB-134 same-binary/same-platform production replays diverged despite identical serialized state and ordered FixedTick mutations");

    kb::project::ProjectDescriptorReadResult descriptor =
        kb::project::ProjectManager::LoadProject(TestRoot() / "Project.21kbproject");
    Require(descriptor.succeeded, "LIB-129 CLI failure-path test could not reload the valid project descriptor");
    const kb::project::ProjectDescriptor validDescriptor = descriptor.descriptor;
    descriptor.descriptor.physicsLayersAsset = "/Game/Config/Missing.21kbphysicslayers";
    Require(
        kb::project::ProjectManager::SaveProject(TestRoot() / "Project.21kbproject", descriptor.descriptor),
        "LIB-129 CLI failure-path test could not persist a missing configured asset");
    const CommandRun missingLayers = Run(&kb::cli::RunRunCommand, {
        "--project", root,
        "--scene", "Assets/Scenes/Main.21kbscene",
        "--frames", "1",
    });
    Require(missingLayers.exitCode == 1,
        "LIB-129 kb_cli silently continued with the default matrix when the configured physics layers asset was missing");
    Require(Contains(missingLayers.output, "project physics layers could not be loaded and applied"),
        "LIB-129 kb_cli did not expose the configured physics layers activation failure");
    Require(
        kb::project::ProjectManager::SaveProject(TestRoot() / "Project.21kbproject", validDescriptor),
        "LIB-129 CLI failure-path test did not restore the valid production fixture descriptor");
}

void RunApiCommandTests() {
    PrepareProject();
    const std::string root = TestRoot().string();

    const CommandRun printed = Run(&kb::cli::RunApiCommand, { "--print", "json" });
    Require(printed.exitCode == 0, "api --print json failed");
    Require(Contains(printed.output, "\"Input.Vector2\""), "api --print json is missing functions");
    Require(
        Contains(printed.output, "\"Timeline.Create\"") &&
            Contains(printed.output, "\"table\":\"Timeline\""),
        "api --print json is missing the direct Timeline Lua surface");

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
    for (const std::string_view sample : { "ThirdPersonController.lua", "TopDownController.lua", "PlatformerController.lua", "SimpleShooterController.lua" }) {
        const std::filesystem::path samplePath = TestRoot() / "Assets" / "Samples" / sample;
        Require(std::filesystem::exists(samplePath), "init-agent did not write a LIB-203 gameplay sample");
        std::ifstream sampleStream{ samplePath, std::ios::binary };
        const std::string source{ std::istreambuf_iterator<char>{ sampleStream }, std::istreambuf_iterator<char>{} };
        Require(Contains(source, "function Tick(self, dt)"), "LIB-203 gameplay sample was not a runnable Tick behaviour");
    }

    const std::filesystem::path audioDemoRoot = TestRoot() / "Assets" / "Samples" / "AudioShooter";
    const std::filesystem::path audioDemoScenePath = TestRoot() / "Assets" / "Scenes" / "AudioShooterDemo.21kbscene";
    for (const std::filesystem::path& demoAsset : {
             audioDemoRoot / "AudioShooterController.lua",
             audioDemoRoot / "AudioProjectile.lua",
             audioDemoRoot / "AudioProjectile.kbprefab",
             audioDemoRoot / "DemoCube.obj",
             audioDemoRoot / "Shot.wav",
             audioDemoRoot / "EngineLoop.wav",
             audioDemoRoot / "Fire.21kbinputaction",
             audioDemoRoot / "AudioShooter.21kbinputcontext",
             audioDemoScenePath,
         }) {
        Require(std::filesystem::is_regular_file(demoAsset), "init-agent did not write a complete Audio Shooter demo asset set");
    }

    const CommandRun validateAudioController = Run(&kb::cli::RunValidateCommand, {
        "--project", root, "Assets/Samples/AudioShooter/AudioShooterController.lua",
    });
    const CommandRun validateAudioProjectile = Run(&kb::cli::RunValidateCommand, {
        "--project", root, "Assets/Samples/AudioShooter/AudioProjectile.lua",
    });
    Require(validateAudioController.exitCode == 0 && validateAudioProjectile.exitCode == 0,
        "Audio Shooter demo scripts did not pass production Lua validation");

    const kb::input::InputAssetLoadResult<kb::input::InputActionAsset> fireAction =
        kb::input::ReadInputAction(audioDemoRoot / "Fire.21kbinputaction");
    const kb::input::InputAssetLoadResult<kb::input::InputMappingContextAsset> audioInputContext =
        kb::input::ReadInputMappingContext(audioDemoRoot / "AudioShooter.21kbinputcontext");
    Require(fireAction.succeeded && fireAction.asset.name == "Fire" &&
            fireAction.asset.valueType == kb::input::InputActionValueType::Bool,
        "Audio Shooter demo Fire input action is invalid");
    const kb::assets::AssetId expectedFireActionId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath("/Game/Samples/AudioShooter/Fire.21kbinputaction") + ":InputAction");
    const kb::assets::AssetId expectedInputContextId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath("/Game/Samples/AudioShooter/AudioShooter.21kbinputcontext") + ":InputMappingContext");
    const kb::assets::AssetId expectedControllerId = kb::assets::MakeAssetId(
        kb::assets::NormalizeAssetPath("/Game/Samples/AudioShooter/AudioShooterController.lua") + ":LuaScript");
    Require(audioInputContext.succeeded && audioInputContext.asset.mappings.size() == 1U &&
            audioInputContext.asset.mappings.front().key == kb::input::InputKey::Space &&
            audioInputContext.asset.mappings.front().actionId == expectedFireActionId.value,
        "Audio Shooter demo Fire action is not bound to Space");

    const kb::scene::SceneDocumentLoadResult audioDemoScene = kb::scene::SceneDocumentService::Load(audioDemoScenePath);
    Require(audioDemoScene.succeeded, "Audio Shooter demo scene could not be loaded");
    const auto findDemoNode = [&audioDemoScene](std::string_view name) -> const kb::scene::ScenePrefabNodeDesc* {
        for (const kb::scene::ScenePrefabNodeDesc& node : audioDemoScene.document.worldPrefab.Nodes()) {
            if (node.name == name) return &node;
        }
        return nullptr;
    };
    const kb::scene::ScenePrefabNodeDesc* shipNode = findDemoNode("Player Ship");
    const kb::scene::ScenePrefabNodeDesc* shipBodyNode = findDemoNode("Ship Body");
    const kb::scene::ScenePrefabNodeDesc* cameraNode = findDemoNode("Follow Camera");
    const kb::scene::ScenePrefabNodeDesc* beaconNode = findDemoNode("Spatial Audio Beacon");
    const std::span<const kb::scene::ScenePrefabNodeDesc> demoNodes = audioDemoScene.document.worldPrefab.Nodes();
    const std::uint32_t shipNodeIndex = shipNode == nullptr
        ? kb::scene::ScenePrefabNodeDesc::NoParent
        : static_cast<std::uint32_t>(shipNode - demoNodes.data());
    Require(shipNode != nullptr && shipBodyNode != nullptr && shipBodyNode->components.meshRenderer.has_value() &&
            shipNode->components.behaviour.has_value() && shipNode->components.input.has_value() &&
            shipNode->components.behaviour->behaviourAssetId == expectedControllerId.value &&
            shipNode->components.input->mappingContextAssetId == expectedInputContextId.value &&
            !shipNode->components.audioSource.has_value(),
        "Audio Shooter ship is not wired to mesh, flight and input without a masking 2D audio source");
    Require(cameraNode != nullptr && cameraNode->parentNode == shipNodeIndex &&
            cameraNode->components.camera.has_value() && cameraNode->components.camera->primary &&
            cameraNode->components.audioListener.has_value() && cameraNode->components.audioListener->primary,
        "Audio Shooter follow camera is not wired to the primary audio listener");
    Require(beaconNode != nullptr && beaconNode->components.audioSource.has_value() &&
            beaconNode->components.audioSource->autoplay && beaconNode->components.audioSource->loop &&
            beaconNode->components.audioSource->spatial &&
            beaconNode->components.audioSource->spatialBlend == 1.0F &&
            beaconNode->components.audioSource->attenuationModel == kb::audio::AudioAttenuationModel::Linear &&
            beaconNode->components.audioSource->maxDistance > beaconNode->components.audioSource->minDistance,
        "Audio Shooter spatial beacon does not demonstrate distance attenuation");

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
        JsonValue parsed;
        std::string error;
        Require(JsonValue::Parse(response, parsed, error), "mcp server emitted invalid JSON");
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

#include "engine/script/ScriptAgentProjectFiles.hpp"

#include "engine/assets/AssetId.hpp"
#include "engine/assets/AssetManager.hpp"
#include "engine/assets/AssetMetadata.hpp"
#include "engine/assets/AssetRegistry.hpp"
#include "engine/library/EngineLibraryAuthoringHints.hpp"
#include "engine/library/EngineLibraryManifest.hpp"
#include "engine/scene/BehaviourComponent.hpp"
#include "engine/scene/ColliderComponent.hpp"
#include "engine/scene/RigidbodyComponent.hpp"
#include "engine/scene/Scene.hpp"
#include "engine/scene/SceneAssets.hpp"
#include "engine/scene/SceneComponents.hpp"
#include "engine/scene/SceneEntities.hpp"
#include "engine/scene/SceneObject.hpp"
#include "engine/scene/SceneObjectDesc.hpp"
#include "engine/scene/ScenePrefabHandle.hpp"
#include "engine/scene/ScenePrefabs.hpp"
#include "engine/script/ScriptAgentGuides.hpp"
#include "engine/script/ScriptApiExport.hpp"
#include "script/AudioShooterDemoProjectFiles.hpp"

#include <fstream>
#include <iterator>
#include <system_error>

namespace kb::script {

namespace {

constexpr std::string_view kAgentsTemplate = R"md(# AGENTS.md - working in this 21kb Engine project

This is a 21kb Engine game project. Keep every request small: read this file, then only the one
guide or API page your task needs. Never read whole folders or the machine files listed below.

## Where things are

- How-to guides, one per kind of task: `.kb/docs/README.md` lists them (objects and materials,
  physics, scripts, characters and animation, combat and AI, 2D maps, checking your work).
  kb-engine tool: `guide` with the guide's name as `topic`.
- Script API, one page per function group, function and component: `.kb/api/README.md` is the
  index. kb-engine tool: `api_reference` with a `topic` (`Physics`, `Physics.Raycast`,
  `Rigidbody`) or a `search` text.
- `Assets/` holds the game, mounted as `/Game/`; `Assets/Scenes/Main.21kbscene` is the startup
  scene. Lua scripts (`*.lua`) are plain text; edit them directly.
- Machine files for tools and editors, not for reading: `.kb/api/script_api.json`,
  `.kb/api/authoring_hints.json`, `.kb/api/manifest.json`, `.kb/api/kb.lua`.

## Rules

- Change scenes, entities, components, materials and other assets only with `kb_cli` (the
  kb-engine `kb_cli` tool runs any command for this project). Scenes, the project file and
  assets are binary: never edit or parse them.
- `kb_cli <command>` with no options prints that command's options.
- Finish with `kb_cli validate --project . <script>` for every script you wrote and
  `kb_cli run --project . --frames 120`; the work is done when it ends with `0 diagnostics`.
)md";

// The AGENTS.md this engine wrote before the guides and the split API reference, recognised
// by its exact bytes (FNV-1a 64) so an untouched copy is brought up to date while one a person
// edited is kept.
constexpr std::string_view kPreviousAgentsTemplateHash = "6927ba90de51399a";

// Claude Code reads CLAUDE.md, not AGENTS.md; this one line imports it.
constexpr std::string_view kClaudeTemplate = "@AGENTS.md\n";

// LIB-013: the minimal PlayerController template AGENTS.md's own worked
// example already references (`--script /Game/Logic/PlayerController.lua`)
// but nothing previously wrote to disk — kAgentsTemplate above documented a
// file that didn't exist. Reads a 2D "Move" input action and translates
// this entity's Transform by speed*dt; Log/Emit calls exist so a fresh
// project's first `kb_cli run` has something observable to confirm the
// wiring (script -> scene -> CLI Play Mode) actually works end to end.
constexpr std::string_view kPlayerControllerLuaTemplate = R"lua(-- PlayerController.lua - minimal starter movement script.
-- Reads a 2D "Move" input action (bind it to WASD/left-stick via an
-- InputMappingContext asset) and moves this entity by speed units/second.
-- Attach with:
--   kb_cli scene-attach --project . --scene Assets/Scenes/Main.21kbscene --node Player --script /Game/Logic/PlayerController.lua

local speed = 2.0

function Ready(self, dt)
    Log("player ready")
end

function Tick(self, dt)
    local move = Input.Vector2("Move")
    local dx = (move.x or 0.0) * speed * dt
    local dy = (move.y or 0.0) * speed * dt
    if dx == 0.0 and dy == 0.0 then
        return
    end
    Transform.Translate(self.entity, dx, dy, 0.0)
    Emit("PlayerMoved", {})
end
)lua";

// LIB-014: the minimal Projectile template — flies straight, destroys itself
// on collision. Shipped as a real, loadable pair: this script plus a baked
// Assets/Prefabs/Projectile.kbprefab (see BakeProjectilePrefab below) with
// Rigidbody+Collider+Behaviour already attached, so World.InstantiatePrefab
// works out of the box. The launch retries every Tick rather than once in
// Ready: a freshly-spawned entity's Rigidbody/Collider is not guaranteed to
// have a live physics body yet by the time Ready fires (script and physics
// scene systems' relative execution order is not guaranteed — see
// PhysicsSceneSystemTests.cpp's LIB-014 proof, which caught exactly this as
// a real bug in an earlier draft), so retrying until Physics.SetVelocity
// actually reports applied=true is the robust, production-correct shape.
//
// "launched" is a @expose'd instance variable, not a plain Lua local — a
// REAL bug PhysicsSceneSystemTests.cpp's LIB-015 proof caught by running TWO
// simultaneous Projectile instances (its own direct-instantiate one plus a
// second one spawned via World.InstantiatePrefab): PucLuaScriptRuntime
// compiles one shared Lua environment per SCRIPT ASSET, not one per entity
// (see PucLuaScriptRuntime.hpp's ScriptRecord, keyed only by assetId) — a
// plain `local launched = false` is therefore SHARED by every entity using
// this same script, so the second projectile saw the first one's
// already-true `launched` and silently never launched at all. self:GetVariable
// /SetVariable are the correctly per-instance-scoped alternative (keyed by
// {entity, assetId} — PucLuaScriptRuntime.hpp's InstanceKey), which is
// exactly what multiple simultaneously spawned prefab instances need.
constexpr std::string_view kProjectileLuaTemplate = R"lua(-- Projectile.lua - minimal starter projectile: flies straight, destroys
-- itself on collision. Spawn the shipped Assets/Prefabs/Projectile.kbprefab
-- with World.InstantiatePrefab; it already carries Rigidbody, Collider, and
-- this script attached as its Behaviour.
--
-- "launched" must be a @expose'd per-instance variable, not a plain local:
-- this script's Lua environment is shared by every entity spawned from this
-- same prefab, so a plain local would leak state between simultaneously
-- flying projectiles.
-- @expose launched Bool = false

local speed = 5.0

function Ready(self, dt)
    Log("projectile ready")
end

function Tick(self, dt)
    if not self:GetVariable("launched") then
        local applied = Physics.SetVelocity(self.entity, speed, 0.0, 0.0)
        if applied then
            self:SetVariable("launched", true)
            Log("projectile launched")
        end
    end
end

function OnCollisionEnter(self, event)
    Log("projectile hit " .. tostring(event.args.other))
    World.Destroy(self.entity)
end
)lua";

// LIB-203: runnable project assets provisioned by the same production path as
// the existing starter scripts. They rely only on registered Input, Physics,
// Transform and World APIs, so the editor and CLI load them as ordinary assets.
constexpr std::string_view kThirdPersonControllerLuaTemplate = R"lua(-- ThirdPersonController.lua
-- Requires: CharacterController. Input: Move (Axis2D), Jump (Bool).
local speed = 5.0
local jumpSpeed = 5.5

function Tick(self, dt)
    local move = Input.Vector2("Move")
    Physics.CharacterMove(self.entity, (move.x or 0.0) * speed, (move.y or 0.0) * speed)
    if Input.Pressed("Jump") then
        Physics.CharacterJump(self.entity, jumpSpeed)
    end
end
)lua";

constexpr std::string_view kTopDownControllerLuaTemplate = R"lua(-- TopDownController.lua
-- Requires: Transform. Input: Move (Axis2D).
-- Screen-plane movement deliberately writes X/Y, not the 3D character backend.
local speed = 6.0

function Tick(self, dt)
    local move = Input.Vector2("Move")
    Transform.Translate(self.entity, (move.x or 0.0) * speed * dt, (move.y or 0.0) * speed * dt, 0.0)
end
)lua";

constexpr std::string_view kPlatformerControllerLuaTemplate = R"lua(-- PlatformerController.lua
-- Requires: CharacterController. Input: Move (Axis2D), Jump (Bool).
local speed = 5.0
local jumpSpeed = 6.0

function Tick(self, dt)
    local move = Input.Vector2("Move")
    Physics.CharacterMove(self.entity, (move.x or 0.0) * speed, 0.0)
    if Input.Pressed("Jump") then
        Physics.CharacterJump(self.entity, jumpSpeed)
    end
end
)lua";

// Ready-made 2D players (kb_cli entity add --create 'Platformer Player' / 'Top Down Player'). Plain Lua files of
// the project; the @expose lines are the tunables the Inspector shows. The CharacterController supplies gravity,
// ground and walls; the platformer script switches on the engine's coyote time for its own character.
constexpr std::string_view kPlatformer2DPlayerLuaTemplate = R"lua(-- Platformer2DPlayer.lua
-- For a Map Platformer (a wall map: x across, y up). Requires: CharacterController.
-- Input: Move (Axis2D, x = left and right), Jump (Bool).
-- @expose speed Float = 6.0
-- @expose jumpSpeed Float = 9.0
-- @expose coyoteTime Float = 0.1
-- @expose jumpBuffer Float = 0.12
-- @expose releaseGravity Float = 3.0
-- @expose fallGravity Float = 1.5
-- coyoteTime: seconds after walking off an edge in which Jump still works (0 to 0.5; this script asks the engine
-- for that window with Physics.SetCharacterJumpGrace, which is off for every other character).
-- jumpBuffer: seconds before landing in which a Jump press is remembered.
-- releaseGravity: gravity multiplier while rising with Jump released (a short hop); fallGravity: while falling.

local states = {}

function Tick(self, dt)
    local entity = self.entity
    local vars = self.variables
    local move = Input.Vector2("Move")
    Physics.CharacterMove(entity, (move.x or 0.0) * vars.speed, 0.0)

    local state = states[entity]
    if state == nil then
        state = { sinceGround = 1000.0, sincePress = 1000.0, gravity = 1.0 }
        states[entity] = state
    end
    -- Ask for the coyote window until the engine has the character, and again whenever the tunable changes.
    if state.grace ~= vars.coyoteTime then
        if Physics.SetCharacterJumpGrace(entity, math.min(math.max(vars.coyoteTime, 0.0), 0.5)) then
            state.grace = vars.coyoteTime
        end
    end
    local grounded = Physics.CharacterIsGrounded(entity)
    local sinceGround = state.sinceGround
    local sincePress = state.sincePress
    if grounded then sinceGround = 0.0 else sinceGround = sinceGround + dt end
    if Input.Pressed("Jump") then sincePress = 0.0 else sincePress = sincePress + dt end

    local velocity = Physics.CharacterVelocity(entity)
    local rising = velocity.found and velocity.y > 0.5
    if rising then
        -- The jump has started: it is used up, and so is the time on the ground.
        sincePress = 1000.0
        sinceGround = 1000.0
    elseif sincePress <= vars.jumpBuffer and sinceGround <= vars.coyoteTime then
        Physics.CharacterJump(entity, vars.jumpSpeed)
    end
    state.sinceGround = sinceGround
    state.sincePress = sincePress

    local gravity = 1.0
    if velocity.found and velocity.y > 0.0 and not Input.Held("Jump") then
        gravity = vars.releaseGravity
    elseif velocity.found and velocity.y < -0.1 then
        gravity = vars.fallGravity
    end
    if gravity ~= state.gravity then
        self:SetProperty("CharacterController", "gravityScale", gravity)
        state.gravity = gravity
    end
end
)lua";

constexpr std::string_view kTopDown2DPlayerLuaTemplate = R"lua(-- TopDown2DPlayer.lua
-- For a Map Top Down (a flat map: x across, z up the screen). Requires: CharacterController with
-- Use Gravity off, so that the player stays on the floor level. Input: Move (Axis2D).
-- @expose speed Float = 5.0
-- @expose eightWay Bool = true
-- eightWay: false allows the four directions only (the stronger axis wins).

function Tick(self, dt)
    local vars = self.variables
    local move = Input.Vector2("Move")
    local x = move.x or 0.0
    local y = move.y or 0.0
    if not vars.eightWay then
        if math.abs(x) >= math.abs(y) then y = 0.0 else x = 0.0 end
    end
    -- A diagonal key press is (1, 1): as fast as one axis once it is normalized. A stick is not boosted.
    local length = math.sqrt(x * x + y * y)
    if length > 1.0 then
        x = x / length
        y = y / length
    end
    Physics.CharacterMove(self.entity, x * vars.speed, y * vars.speed)
end
)lua";

// The same two players on the 2D physics (Box2D): a Character Controller 2D driven through the Physics2D functions.
constexpr std::string_view kPlatformer2DBox2DPlayerLuaTemplate = R"lua(-- Platformer2DBox2DPlayer.lua
-- For a Map Platformer (a wall map: x across, y up) on the 2D physics. Requires: Character Controller 2D (side view).
-- Input: Move (Axis2D, x = left and right), Jump (Bool).
-- @expose speed Float = 6.0
-- @expose jumpSpeed Float = 9.0
-- @expose jumpBuffer Float = 0.12
-- jumpBuffer: seconds before landing in which a Jump press is remembered.

local states = {}

function Tick(self, dt)
    local entity = self.entity
    local vars = self.variables
    local move = Input.Vector2("Move")
    CallFunction("Physics2D.MoveCharacter", { entity = entity, x = (move.x or 0.0) * vars.speed, y = 0.0 })

    local sincePress = states[entity]
    if sincePress == nil then sincePress = 1000.0 end
    if Input.Pressed("Jump") then sincePress = 0.0 else sincePress = sincePress + dt end
    if sincePress <= vars.jumpBuffer and CallFunction("Physics2D.IsGrounded", { entity = entity }) then
        if CallFunction("Physics2D.JumpCharacter", { entity = entity, speed = vars.jumpSpeed }) then sincePress = 1000.0 end
    end
    states[entity] = sincePress
end
)lua";

constexpr std::string_view kPlatformer2DNavigatorLuaTemplate = R"lua(-- Platformer2DNavigator.lua
-- Drives the native 2D controller along live tilemap routes without changing transforms.
-- @expose mapName String = "Map"
-- @expose targetX Int = 6
-- @expose targetY Int = 6
-- @expose floor Int = 0
-- @expose speed Float = 4.0
-- @expose jumpSpeed Float = 9.0
-- @expose jumpHeight Int = 3
-- @expose jumpDistance Int = 4
local states = {}
local function report(state, status)
    if state.status ~= status then Log("navigation-" .. status); state.status = status end
end
function Tick(self, dt)
    local e, v = self.entity, self.variables
    local s = states[e] or {elapsed=0}; states[e] = s
    local grounded = CallFunction("Physics2D.IsGrounded", {entity=e})
    if s.flight then
        s.elapsed = s.elapsed + dt
        if not grounded then s.leftGround = true end
        if grounded and s.leftGround then s.flight = false end
        if s.elapsed > 4 then
            s.flight = false; s.x = nil
            report(s,"unreachable")
        end
    end
    if grounded and not s.flight then
        local map = World.FindByName(v.mapName)
        if map == nil or map == 0 then
            s.x = nil; report(s,"missing-map")
        else
            local next = CallFunction("Map2D.PlatformerWaypoint", {entity=map,character=e,
                targetX=v.targetX,targetY=v.targetY,floor=v.floor,jumpHeight=v.jumpHeight,jumpDistance=v.jumpDistance})
            if not next or not next.found then
                s.x = nil; report(s,"unreachable")
            elseif next.reached then
                s.x = nil; report(s,"reached")
            else
                s.x = next.x; report(s,"moving")
                if next.jump then
                    s.flight = CallFunction("Physics2D.JumpCharacter", {entity=e,speed=v.jumpSpeed})
                    s.elapsed = 0; s.leftGround = false
                end
            end
        end
    end
    local x = Transform.GetPosition(e).x
    local velocity = s.x and math.max(-v.speed,math.min(v.speed,(s.x-x)/math.max(dt,0.001))) or 0
    CallFunction("Physics2D.MoveCharacter", {entity=e,x=velocity,y=0})
end
)lua";

constexpr std::string_view kTopDown2DBox2DPlayerLuaTemplate = R"lua(-- TopDown2DBox2DPlayer.lua
-- For a Map Top Down (a flat map: x across, z up the screen) on the 2D physics. Requires: Character Controller 2D
-- with Side View off. Input: Move (Axis2D).
-- @expose speed Float = 5.0
-- @expose eightWay Bool = true
-- eightWay: false allows the four directions only (the stronger axis wins).

function Tick(self, dt)
    local vars = self.variables
    local move = Input.Vector2("Move")
    local x = move.x or 0.0
    local y = move.y or 0.0
    if not vars.eightWay then
        if math.abs(x) >= math.abs(y) then y = 0.0 else x = 0.0 end
    end
    local length = math.sqrt(x * x + y * y)
    if length > 1.0 then
        x = x / length
        y = y / length
    end
    CallFunction("Physics2D.MoveCharacter", { entity = self.entity, x = x * vars.speed, y = y * vars.speed })
end
)lua";

constexpr std::string_view kSimpleShooterControllerLuaTemplate = R"lua(-- SimpleShooterController.lua
-- Requires: CharacterController and the shipped Projectile.kbprefab.
-- Input: Move (Axis2D), Fire (Bool).
local speed = 5.0

function Tick(self, dt)
    local move = Input.Vector2("Move")
    Physics.CharacterMove(self.entity, (move.x or 0.0) * speed, (move.y or 0.0) * speed)
    if Input.Pressed("Fire") then
        local position = Transform.GetPosition(self.entity)
        if position ~= nil then
            World.InstantiatePrefab({ prefab = "/Game/Prefabs/Projectile.kbprefab", x = position.x, y = position.y, z = position.z })
        end
    end
end
)lua";

constexpr std::string_view kLuarcTemplate = R"json({
    "$schema": "https://raw.githubusercontent.com/LuaLS/vscode-lua/master/setting/schema.json",
    "runtime.version": "Lua 5.4",
    "workspace.library": [".kb/api"],
    "workspace.ignoreDir": [".kb"],
    "diagnostics.globals": [
        "Created", "Activated", "Ready", "FixedTick", "Tick", "LateTick",
        "BeforeRender", "AfterRender", "Deactivated", "Destroyed"
    ]
}
)json";

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, std::string_view content, std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        error = "could not open file for writing: " + path.string();
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output.good()) {
        error = "could not write file: " + path.string();
        return false;
    }
    return true;
}

// LIB-014: bakes Assets/Prefabs/Projectile.kbprefab headlessly — the same,
// already-proven pattern PhysicsSceneSystemTests.cpp's LIB-015 section uses
// (throwaway discovery Scene to resolve Projectile.lua's real AssetId, then
// a fresh prefab-source Scene to capture+save) — no editor or live physics
// backend required. Component values mirror the ones proved to fly, collide,
// and self-destroy correctly under real Jolt physics in that same test file.
// Must run AFTER Projectile.lua is physically on disk: resolving its AssetId
// requires discovering it as a real project asset first.
[[nodiscard]] bool BakeProjectilePrefab(const std::filesystem::path& projectRoot, std::string& error) {
    kb::assets::AssetId scriptAssetId{};
    {
        kb::scene::Scene discoveryScene;
        if (!discoveryScene.Assets().MountProject(projectRoot)) {
            error = "could not mount project to resolve Projectile.lua's asset id";
            return false;
        }
        static_cast<void>(discoveryScene.Assets().Discover());
        const kb::assets::AssetMetadata* metadata = discoveryScene.Assets().Manager().Registry().FindByPath("/Game/Logic/Projectile.lua");
        if (metadata == nullptr) {
            error = "could not resolve Projectile.lua's asset id";
            return false;
        }
        scriptAssetId = metadata->id;
    }

    kb::scene::Scene prefabSource;
    const kb::scene::SceneObject root = prefabSource.Entities().CreateObject(kb::scene::SceneObjectDesc{ .name = "Projectile" });
    prefabSource.Components().Rigidbodies().Set(root.Entity(), kb::scene::RigidbodyComponent{
                                                                    .bodyType = kb::scene::RigidbodyBodyType::Dynamic,
                                                                    .mass = 0.5F,
                                                                    .useGravity = false,
                                                                });
    prefabSource.Components().Colliders().Set(root.Entity(), kb::scene::ColliderComponent{
                                                                  .shape = kb::scene::ColliderShape::Sphere,
                                                                  .radius = 0.3F,
                                                              });
    prefabSource.Components().Behaviours().Set(root.Entity(), kb::scene::BehaviourComponent{
                                                                   .behaviourAssetId = scriptAssetId.value,
                                                                   .backend = kb::scene::BehaviourBackend::Lua,
                                                                   .enabled = true,
                                                               });
    const kb::scene::ScenePrefabHandle prefab = prefabSource.Prefabs().CaptureRegistered(root, "Projectile");
    if (!prefabSource.Prefabs().Save(prefab, projectRoot / "Assets" / "Prefabs" / "Projectile.kbprefab")) {
        error = "could not save Projectile.kbprefab";
        return false;
    }
    return true;
}

} // namespace

std::string_view ScriptAgentProjectFiles::PresetScript(std::string_view fileName) noexcept {
    if (fileName == "Platformer2DNavigator.lua") return kPlatformer2DNavigatorLuaTemplate;
    if (fileName == "Platformer2DPlayer.lua") return kPlatformer2DPlayerLuaTemplate;
    if (fileName == "TopDown2DPlayer.lua") return kTopDown2DPlayerLuaTemplate;
    if (fileName == "Platformer2DBox2DPlayer.lua") return kPlatformer2DBox2DPlayerLuaTemplate;
    if (fileName == "TopDown2DBox2DPlayer.lua") return kTopDown2DBox2DPlayerLuaTemplate;
    return {};
}

ScriptAgentProjectFilesResult ScriptAgentProjectFiles::Write(
    const std::filesystem::path& projectRoot,
    const ScriptApiCatalog& catalog) {
    ScriptAgentProjectFilesResult result;

    std::error_code errorCode;
    if (!std::filesystem::exists(projectRoot, errorCode) || errorCode) {
        result.error = "project root does not exist: " + projectRoot.string();
        return result;
    }

    const std::filesystem::path apiRoot = projectRoot / ".kb" / "api";
    std::filesystem::create_directories(apiRoot, errorCode);
    if (errorCode) {
        result.error = "could not create directory: " + apiRoot.string();
        return result;
    }

    // LIB-013: PlayerController.lua lives under Assets/Logic/, same as any
    // other project behaviour script — that directory does not exist yet in
    // a freshly-scaffolded project (only .kb/api/ is created above).
    const std::filesystem::path logicRoot = projectRoot / "Assets" / "Logic";
    std::filesystem::create_directories(logicRoot, errorCode);
    if (errorCode) {
        result.error = "could not create directory: " + logicRoot.string();
        return result;
    }
    const std::filesystem::path sampleRoot = projectRoot / "Assets" / "Samples";
    std::filesystem::create_directories(sampleRoot, errorCode);
    if (errorCode) {
        result.error = "could not create directory: " + sampleRoot.string();
        return result;
    }

    struct GeneratedFile {
        std::filesystem::path path;
        std::string content;
        bool overwrite = true;
        // LIB-013: see ScriptAgentProjectFilesResult::wroteProjectAsset.
        bool isProjectAsset = false;
    };

    const kb::library::ApiManifest manifest = kb::library::BuildApiManifest(catalog);
    const std::string referenceMarkdown = kb::library::ToReferenceMarkdown(manifest);
    const kb::library::ApiReferenceValidationResult referenceValidation =
        kb::library::ValidateReferenceMarkdown(manifest, referenceMarkdown);
    if (!referenceValidation.Succeeded()) {
        result.error = "generated API reference diverged from its manifest: " + referenceValidation.errors.front();
        return result;
    }

    // The reference an agent reads is split into small pages (an index, one page per function
    // group, function and component) and the how-to guides sit beside it, so a request reads
    // only what it needs. Both folders are regenerated whole, and the single-file reference
    // older versions wrote is removed.
    const std::filesystem::path docsRoot = projectRoot / ".kb" / "docs";
    std::filesystem::remove(apiRoot / "script_api.md", errorCode);
    std::filesystem::remove_all(apiRoot / "functions", errorCode);
    std::filesystem::remove_all(apiRoot / "components", errorCode);
    std::filesystem::remove_all(docsRoot, errorCode);
    for (const ScriptApiReferencePage& page : ScriptApiExport::ToReferencePages(catalog)) {
        const std::filesystem::path path = apiRoot / std::filesystem::path{ std::u8string{ reinterpret_cast<const char8_t*>(page.path.data()), page.path.size() } };
        std::filesystem::create_directories(path.parent_path(), errorCode);
        if (!WriteTextFile(path, page.content, result.error)) {
            return result;
        }
    }
    std::filesystem::create_directories(docsRoot, errorCode);
    if (!WriteTextFile(docsRoot / "README.md", ScriptAgentGuides::Index(), result.error)) {
        return result;
    }
    for (const ScriptAgentGuide& guide : ScriptAgentGuides::All()) {
        if (!WriteTextFile(docsRoot / (std::string{ guide.name } + ".md"), std::string{ guide.body }, result.error)) {
            return result;
        }
    }
    result.writtenFiles.push_back(apiRoot / "README.md");
    result.writtenFiles.push_back(docsRoot / "README.md");

    // An AGENTS.md this engine wrote and nobody changed since is brought up to date.
    const std::filesystem::path agentsPath = projectRoot / "AGENTS.md";
    if (std::ifstream existing(agentsPath, std::ios::binary); existing.is_open()) {
        const std::string content{ std::istreambuf_iterator<char>(existing), std::istreambuf_iterator<char>() };
        existing.close();
        if (kb::library::ComputeApiManifestHash(content) == kPreviousAgentsTemplateHash) {
            std::filesystem::remove(agentsPath, errorCode);
        }
    }

    const GeneratedFile files[] = {
        { apiRoot / "kb.lua", ScriptApiExport::ToLuaStubs(catalog), true, false },
        { apiRoot / "script_api.json", ScriptApiExport::ToJson(catalog), true, false },
        { apiRoot / "manifest.json", kb::library::ToJson(manifest), true, false },
        { apiRoot / "authoring_hints.json", kb::library::ToAuthoringHintsJson(manifest), true, false },
        { agentsPath, std::string{ kAgentsTemplate }, false, false },
        { projectRoot / "CLAUDE.md", std::string{ kClaudeTemplate }, false, false },
        { projectRoot / ".luarc.json", std::string{ kLuarcTemplate }, false, false },
        // Write-once like AGENTS.md/.luarc.json: a game author's edits to
        // their own PlayerController.lua must never be clobbered by a later
        // `kb_cli init-agent` re-run (which DOES always regenerate .kb/api/).
        // isProjectAsset=true: unlike every other generated file here, this
        // one lands under Assets/ and becomes discoverable project content.
        { logicRoot / "PlayerController.lua", std::string{ kPlayerControllerLuaTemplate }, false, true },
        // LIB-014: same write-once/isProjectAsset treatment as
        // PlayerController.lua above.
        { logicRoot / "Projectile.lua", std::string{ kProjectileLuaTemplate }, false, true },
        { sampleRoot / "ThirdPersonController.lua", std::string{ kThirdPersonControllerLuaTemplate }, false, true },
        { sampleRoot / "TopDownController.lua", std::string{ kTopDownControllerLuaTemplate }, false, true },
        { sampleRoot / "PlatformerController.lua", std::string{ kPlatformerControllerLuaTemplate }, false, true },
        { sampleRoot / "SimpleShooterController.lua", std::string{ kSimpleShooterControllerLuaTemplate }, false, true },
    };

    for (const GeneratedFile& file : files) {
        if (!file.overwrite && std::filesystem::exists(file.path, errorCode) && !errorCode) {
            result.skippedFiles.push_back(file.path);
            continue;
        }
        if (!WriteTextFile(file.path, file.content, result.error)) {
            return result;
        }
        result.writtenFiles.push_back(file.path);
        result.wroteProjectAsset = result.wroteProjectAsset || file.isProjectAsset;
    }

    // LIB-014: Assets/Prefabs/Projectile.kbprefab — write-once like the
    // scripts above (a game author's own prefab edits, e.g. in the editor,
    // must never be clobbered by a later init-agent run). Baked AFTER the
    // loop above: baking resolves Projectile.lua's real AssetId by
    // discovering it as a project asset, which requires it to already be on
    // disk (see BakeProjectilePrefab).
    const std::filesystem::path prefabPath = projectRoot / "Assets" / "Prefabs" / "Projectile.kbprefab";
    if (std::filesystem::exists(prefabPath, errorCode) && !errorCode) {
        result.skippedFiles.push_back(prefabPath);
    } else {
        std::filesystem::create_directories(prefabPath.parent_path(), errorCode);
        if (errorCode) {
            result.error = "could not create directory: " + prefabPath.parent_path().string();
            return result;
        }
        if (!BakeProjectilePrefab(projectRoot, result.error)) {
            return result;
        }
        result.writtenFiles.push_back(prefabPath);
        result.wroteProjectAsset = true;
    }

    if (!WriteAudioShooterDemoProjectFiles(projectRoot, result)) {
        return result;
    }

    result.succeeded = true;
    return result;
}

} // namespace kb::script

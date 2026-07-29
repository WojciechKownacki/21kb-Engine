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
#include "engine/script/ScriptApiExport.hpp"

#include <fstream>
#include <system_error>

namespace kb::script {

namespace {

constexpr std::string_view kAgentsTemplate = R"md(# AGENTS.md — working in this 21kb Engine project

This is a game project for the 21kb Engine. Read this file before writing gameplay code.

## Project layout

- `Project.21kbproject` — binary project descriptor. Never edit by hand; use the editor or `kb_cli`.
- `Assets/` — all game content, mounted as virtual root `/Game/`.
  - `Assets/Scenes/*.21kbscene` — scenes (binary; inspect/modify with `kb_cli scene-list` / `kb_cli scene-attach`).
  - `Assets/**/*.lua` — Lua behaviour scripts (plain text, edit freely).
  - `Assets/**/*.kbgraph` — visual-graph behaviours (line-based text format).
- `.kb/api/` — generated API reference and Lua stubs. Regenerate with `kb_cli api --project .` after engine updates.
- `.luarc.json` — Lua Language Server configuration pointing at the stubs; keep it so completion and diagnostics work.

## Writing a behaviour script

A behaviour is a `.lua` file under `Assets/`. Implement lifecycle hooks as global functions named exactly
after the lifecycle event, each receiving `(self, dt)`:

```lua
-- @expose speed Float = 6.0

function Ready(self, dt)
    Log("spawned entity " .. tostring(self.entity))
end

function Tick(self, dt)
    local move = Input.Vector2("Move")
    local x = self:GetProperty("Transform", "localPosition.x")
    self:SetProperty("Transform", "localPosition.x", x + move.x * self:GetVariable("speed") * dt)
end

function DoorOpened(self, event) -- custom event handler, receives (self, event)
    Log(event.args.door)
end
```

Lifecycle order: Created, Activated, Ready, then per frame FixedTick (fixed step), Tick, LateTick,
BeforeRender, AfterRender; Deactivated and Destroyed on teardown.

- Full API reference: `.kb/api/script_api.md` (functions, components, properties, types).
- The sandbox has no `io`, `os`, `require`, or `load`. Use `Import("Module.Name")` for `-- @import` modules.
- `-- @expose <name> <Type> [= default]` declares inspector-editable variables; read them with `self:GetVariable`.

## Attaching a behaviour to a scene entity

Scenes are binary. Do not edit them with a text editor. Use the CLI:

```
kb_cli scene-list   --project . --scene Assets/Scenes/Main.21kbscene
kb_cli scene-attach --project . --scene Assets/Scenes/Main.21kbscene --node Player --script /Game/Logic/PlayerController.lua
```

## Shipped gameplay samples

`kb_cli init-agent` also provisions four runnable scripts under
`Assets/Samples/`: `ThirdPersonController.lua`, `TopDownController.lua`,
`PlatformerController.lua`, and `SimpleShooterController.lua`. They use the
same Input, Physics, Transform, World, and prefab APIs as ordinary gameplay
scripts. Add the documented components, attach one script to a player entity,
and bind its named input actions before entering Play Mode.

## Verifying your work (always do this before finishing)

```
kb_cli validate --project . Assets/Logic/PlayerController.lua   # syntax + sandbox check
kb_cli run --project . --scene Assets/Scenes/Main.21kbscene --frames 120   # headless run; prints Log output, events, script errors
```

A change is done only when `validate` reports OK and `run` finishes without script diagnostics.
)md";

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

    const GeneratedFile files[] = {
        { apiRoot / "kb.lua", ScriptApiExport::ToLuaStubs(catalog), true, false },
        { apiRoot / "script_api.md", referenceMarkdown, true, false },
        { apiRoot / "script_api.json", ScriptApiExport::ToJson(catalog), true, false },
        { apiRoot / "manifest.json", kb::library::ToJson(manifest), true, false },
        { apiRoot / "authoring_hints.json", kb::library::ToAuthoringHintsJson(manifest), true, false },
        { projectRoot / "AGENTS.md", std::string{ kAgentsTemplate }, false, false },
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

    result.succeeded = true;
    return result;
}

} // namespace kb::script

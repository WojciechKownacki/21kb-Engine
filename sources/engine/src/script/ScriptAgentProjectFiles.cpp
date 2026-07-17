#include "engine/script/ScriptAgentProjectFiles.hpp"

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
    Transform.Translate(self.entity, dx, dy, 0.0)
    Log("player tick")
    Emit("PlayerMoved", {})
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

    struct GeneratedFile {
        std::filesystem::path path;
        std::string content;
        bool overwrite = true;
        // LIB-013: see ScriptAgentProjectFilesResult::wroteProjectAsset.
        bool isProjectAsset = false;
    };

    const GeneratedFile files[] = {
        { apiRoot / "kb.lua", ScriptApiExport::ToLuaStubs(catalog), true, false },
        { apiRoot / "script_api.md", ScriptApiExport::ToMarkdown(catalog), true, false },
        { apiRoot / "script_api.json", ScriptApiExport::ToJson(catalog), true, false },
        { projectRoot / "AGENTS.md", std::string{ kAgentsTemplate }, false, false },
        { projectRoot / ".luarc.json", std::string{ kLuarcTemplate }, false, false },
        // Write-once like AGENTS.md/.luarc.json: a game author's edits to
        // their own PlayerController.lua must never be clobbered by a later
        // `kb_cli init-agent` re-run (which DOES always regenerate .kb/api/).
        // isProjectAsset=true: unlike every other generated file here, this
        // one lands under Assets/ and becomes discoverable project content.
        { logicRoot / "PlayerController.lua", std::string{ kPlayerControllerLuaTemplate }, false, true },
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

    result.succeeded = true;
    return result;
}

} // namespace kb::script

# AI-assisted scripting — quick guide

You open your game project in VS Code, write a prompt like *"create third-person player
movement and attach a follow camera"*, and the AI agent writes the Lua scripts, wires
them into the scene, and checks that they work. The engine ships a small tool,
`kb_cli`, that makes this possible.

## Which language to start with

**Start with Lua.** It is the language the templates, the API reference, the agent tools and the
examples are written for; scripts are plain text files, a mistake is reported with its file and line
and never needs a rebuild, and the packaged game runs them in the same sandbox as the editor. C++
(`script-new --language cpp`) and Visual Graph are options for when a project needs them: C++ for
code that must be as fast as the engine and can use any library, with a toolchain to install and a
module to build; Visual Graph for logic drawn as nodes. Nothing in the engine requires them.

A first game from the template, with every step available to `kb_cli`:

1. `kb_cli init-agent --project <dir>` writes `PlayerController.lua` (moves the object from the input
   actions) and `Projectile.lua`.
2. `kb_cli scene-attach` puts the controller on the player; `kb_cli validate` checks the script.
3. `kb_cli run --frames 120` plays the scene without a window and reports script errors; the tests of
   the engine run the same template with a key held and check that the Transform moves.
4. Scripts read input with `Input.*`, move bodies with `Physics.*`, show and read widgets with `UI.*`
   and keep progress with `Save.Write` / `Save.Read` (a save slot per name, atomic, with a size limit,
   see `release_security.md`).
5. Export is done by the editor's Build Game command or the packaging scripts; the packaged game uses
   the same scripts and the same save files.

For a first top-down character that collides with walls, use the supplied controller
instead of the minimal translation example. In an existing empty project:

```sh
kb_cli entity add --project . --create "Top Down Player" --physics 2d --name Player --position 0,0,0
kb_cli entity add --project . --name Wall --position 2,0,0
kb_cli component add --project . --entity Wall --type Collider2D --set width=1 --set height=10 --set sideView=false
```

The preset writes a Lua controller and WASD/arrow/gamepad mappings. Holding right
moves toward the wall and stops at it; left moves away again. `sideView=false`
puts this collider in the top-down X/Z plane. The Platformer Player preset uses
gravity and the side-view X/Y plane instead. Both controller scripts remain ordinary
project files with editable `speed` variables.

Assets loaded by path from Lua must be declared for export. Put one `-- @asset`
comment per canonical virtual path in the script's opening comment block, before
any Lua code or long comment. For example:

```lua
-- @asset /Game/Prefabs/Crate.kbprefab
function Ready(self, dt)
    local entity, diagnostic = CallFunction("World.Spawn", {
        prefab = "/Game/Prefabs/Crate.kbprefab", x = 7, y = 2, z = 3
    })
    assert(entity, diagnostic)
end
```

The cooker includes these assets and their dependencies. Missing assets, noncanonical
paths and editor-only assets fail validation. This also supports paths assembled at
runtime: declare each possible target. Update declarations when moving assets.
The declaration does not grant filesystem access or execute Lua during discovery.
Project Files refuses a rename or move that would break registered references,
including references between files in the same folder. Deleting a folder is allowed
when all users of its assets are deleted with it; surviving users prevent deletion.
Copying scenes and prefabs assigns new asset identities while preserving stable node
IDs and variant overrides. When copying a folder, nested prefab references and variant
base references within that copy point to the copied prefabs. References to prefabs
outside the folder retain their original targets. Path-based resource references and
Lua asset declarations are not rewritten by copying. Malformed or ambiguous prefab
graphs refuse the copy without modifying the source.

## What a script is allowed to do

Lua scripts run in a sandbox. A script gets its own copy of `assert`, `error`, `ipairs`, `pairs`,
`pcall`, `select`, `tonumber`, `tostring`, `type` and the `coroutine`, `table`, `string`, `math` and
`utf8` libraries. There is no `os`, `io`, `debug` or `package`, and `load`, `loadfile`, `dofile`,
`require` and `collectgarbage` do not exist; text chunks only. The only files a script can reach are its
own save slots (through `Save.*`, with a quota), and the only network access is the `Net.*` API. A
script cannot start a process.

Limits, per behaviour call: the instruction budget (`luaInstructionsPerBehaviour`, one million by default; also applied to the
code at the top of a script while it loads, so a loop there fails the load) and a memory cap per Lua
runtime (256 MiB by default). A script that exceeds a limit fails with a diagnostic naming the limit and
the script; the shipped hosts then disable that behaviour (`disableFailingBehaviours`), and the other
scripts keep running. Trusted hosts can explicitly disable the instruction cap by setting it to zero.
The cap also applies to script-created coroutines. It counts Lua instructions, not elapsed time;
native API calls are not preempted, and there is no wall-clock limit.

**Native C++ scripts and plugins are not sandboxed.** They run inside the game process with all of its
rights and can do anything the game can. Load them only from projects you trust; the engine
checks their hash against the release manifest in a packaged game but does not isolate them.
Mod manifests cannot introduce native plugins. The player validates this restriction again when
loading installed mods, including folders copied into place without the installer. Mod Lua scripts
use the same restricted standard libraries as project Lua scripts; they can still change gameplay
through the exposed engine API and are not isolated from the game's state.

The packaged Lua workflow regression (`sources/game/tests/web/run_game_lua_workflow_tests.py`)
creates a project through the public CLI, generates the unchanged `PlayerController.lua` template,
authors input bindings and a collider, and cooks the game. Trusted keyboard input must move the
template entity. The supplied top-down character must stop at a solid wall under continued
input and then move away; a physics query and movement update real UI pixels. Clicking the native button
saves the observed position, and a fresh browser process must read the same value from the save.
The template starts at its authored position; restoring gameplay transforms from saved values
remains an explicit script decision.

The Windows `kb_game_native_sdk_package` test generates and builds an external C++ module,
attaches it, cooks its DLL into `RuntimeModules`, and runs the package after moving the source
project away. It also verifies an unavailable compiler and a compiled DLL accepting an older
registration API version. Failed builds retain the compiler output and suggest installing the
required toolchain, correcting the `.native` build command and retrying `script-build`;
incompatible modules instruct the author to rebuild against the current SDK.

## Hot reload

Reloading a Lua script runs the new code in a fresh environment and swaps it in only if that works: a
syntax error, or an error in the code at the top of the script, keeps the running version, its
variable schema and its hash, and reports the error with file and line. Import modules register
lazily the first time; replacing an existing module evaluates the candidate before releasing its
previous cached value. Syntax, runtime and execution-budget failures retain the working module.
Nested imports preserve the caller's execution budget and debugger hook.

A successful behaviour reload restarts its lifecycle (Created, Ready) and discards the old Lua
environment. Per-instance exposed-variable overrides retain their values by name and compatible
type; untouched defaults refresh from the new schema. A conflicting type is diagnosed rather than
silently converted. Arbitrary Lua locals are not migrated. Shared values (`SetShared`), saves and
scene data stay. The tests replace Lua scripts and their imported modules 200 times while checking
bounded Lua memory and released coroutines.

Native reload preserves host-owned shared state and asset bindings, replaces callbacks and unloads
the previous library. Module-local static state belongs to the replacement library. Failed loads
retain the working callbacks. The native test repeats 80 replacements and ten failed attempts;
on Windows it also checks that old modules are unmapped and private committed memory remains
bounded after warmup. This memory measurement is reported as unavailable on other test platforms.

## Debugging

The Lua runtime can pause a script at a breakpoint, step into, over and out, continue, and show the call
stack and locals. A paused script is suspended, not blocking: the game loop and the other scripts keep
running. When the debugger goes away, clear its settings and resume, and the script runs to the end.
There is no evaluate command and no `kb_cli` debug session yet.

## Setup

**Once per machine** — build the tool and add it to `PATH`:

```powershell
cmake --build build --config Debug --target kb_cli
```

The binary lands in `build\bin\Debug\kb_cli.exe`.

**Once per project** — prepare the project for agents:

```powershell
kb_cli init-agent --project <project dir>
```

This adds `AGENTS.md` (instructions the agent reads automatically) and an API reference
with editor autocompletion support.

**In VS Code:**

1. Install the **Lua** extension — you get autocompletion for the engine API.
2. Connect the engine to your MCP-compatible agent:

```powershell
<agent-cli> mcp add kb-engine -- <path>\kb_cli.exe mcp --project <project dir>
```

## How to work

- **You** create scenes, nodes, and assets in the engine editor.
- **The agent** writes the gameplay scripts and attaches them to your scene nodes.

Example prompt:

> Create a third-person movement script for the Player node and a follow camera script
> for the Camera node. Scene: Assets/Scenes/Main.21kbscene. Verify it works.

Behind the scenes the agent validates its scripts, attaches them to the scene, and runs
the scene headless to catch errors — then fixes them on its own. When it's done, open the
scene in the editor and press Play.

## Commands (mostly used by the agent)

| Command | Purpose |
|---|---|
| `kb_cli init-agent --project <dir>` | prepare a project for AI agents |
| `kb_cli api --project <dir>` | refresh the API reference after engine updates |
| `kb_cli script-new --project <dir> --name <Name> --language lua\|cpp` | create a Lua or C++ script from the template (never overwrites) |
| `kb_cli script-build --project <dir> [--script <path>]` | build the project's C++ scripts |
| `kb_cli validate <file.lua>` | check a script for errors |
| `kb_cli scene-list --project <dir> --scene <path>` | show what's in a scene |
| `kb_cli scene-attach --project <dir> --scene <path> --node <name> --script <path>` | attach a script to a node |
| `kb_cli run --project <dir> --scene <path> --frames <n>` | run a scene without the editor and report errors |
| `kb_cli run --project <dir> --frames <n> --realtime` | pace fixed simulation steps by wall time so asynchronous streaming and external services can run during playback |
| `kb_cli run --project <dir> --frames <n> --timeout-ms <ms> --cancel-file <path>` | stop playback cooperatively on a wall-time limit or when the cancellation file exists; shutdown still runs; exit 124 means timeout and 130 means cancelled |
| `kb_cli batch --project <dir> --file <list> [--dry-run]` | run many commands as one all-or-nothing transaction, or preview their changes |
| `kb_cli mod <build\|install\|list\|verify\|enable\|disable\|remove\|trust\|info>` | build content mods and manage their installation, dependencies and trust; see [Modding](modding.md) for arguments and package compatibility |

More commands, each with its options in `kb_cli help`:

| Command | Purpose |
|---|---|
| `kb_cli new` | create a project in a new or empty folder (Basic or Empty startup scene) |
| `kb_cli scene-new` | create a scene from a template |
| `kb_cli entity` | add or remove a scene entity |
| `kb_cli component` | add, set or remove a component of an entity |
| `kb_cli schema` | list components and their properties |
| `kb_cli api-check` | compare the script API against a committed baseline and fail on breaking changes |
| `kb_cli material-new` | create a material with its values set at once |
| `kb_cli material-set` | change values of a material |
| `kb_cli material-show` | print the values of a material |
| `kb_cli material-graph` | edit a material graph node by node |
| `kb_cli material-instance-new` | create a material instance with overrides |
| `kb_cli material-instance-set` | change or clear overrides of a material instance |
| `kb_cli material-instance-show` | print a material instance and its effective values |
| `kb_cli post-process-new` | create a post-process profile |
| `kb_cli post-process-set` | change values of a post-process profile |
| `kb_cli scene-post-process` | give a scene its post-process profile |
| `kb_cli audio-mixer` | create, edit and assign an audio bus graph |
| `kb_cli animator` | build an animator controller |
| `kb_cli skeleton` | read a character skeleton and edit its sockets |
| `kb_cli ability` | create and edit combat abilities |
| `kb_cli ai` | give an object a combatant and an AI brain from a preset |
| `kb_cli map2d` | create and edit 2D tile maps |
| `kb_cli vfx-new` | create a particle effect |
| `kb_cli vfx-show` | print a particle effect |
| `kb_cli vfx-set` | change values of a particle effect |
| `kb_cli vfx-emitter-add` | add an emitter to a particle effect |
| `kb_cli vfx-emitter-remove` | remove an emitter from a particle effect |
| `kb_cli vfx-module-add` | add a module to an emitter |
| `kb_cli vfx-module-remove` | remove a module from an emitter |
| `kb_cli vfx-profile` | profile an isolated particle effect on a hidden GPU host (Windows) |
| `kb_cli renderer-info` | print the graphics backend a hidden host selects, as JSON (Windows) |
| `kb_cli import` | import external source assets into the project |
| `kb_cli import-preset-new` | create an import preset |
| `kb_cli mcp` | serve the commands as MCP tools over stdio |
| `kb_cli commands` | list registered commands, build availability and MCP tool names as JSON |
| `kb_cli keys` | generate release signing keys and print public keys |
| `kb_cli pack` | sign, verify, inspect, recompress, split and patch asset packs |
| `kb_cli release` | sign or verify the manifest of a finished release directory |
| `kb_cli world` | convert a scene into a partitioned world or build its streaming cells |
| `kb_cli navmesh` | bake a scene navigation mesh or describe a baked one |
| `kb_cli physics-layers` | show or edit the layer collision matrix |
| `kb_cli lighting-quality` | show or set the shadow and global illumination quality |

## Inspecting AI decisions

`kb_cli run --project <dir> --frames 60 --report ai` writes a clean JSON report
(`21kb.ai-decisions/v1`) to stdout and runtime logs to stderr. With the
`Gameplay.AI` provider active, each visited brain reports its last selected rule,
condition and threshold, action and parameter, target, and decision frame.
`rule_index` is zero-based; `-1` means no matching rule. Authoring with `ai rule`
uses rule numbers 1–8. A missing target is JSON `null`.

The brain is an ordered list of at most eight rules with presets, not a behavior
tree or planner. Ordinary decisions run at staggered 0.2-second intervals.
Changing authored rules or parameters interrupts the old action and evaluates
the new rules on the next runtime update. Diagnostics distinguish pending,
running, disabled, inactive and dead brains; removing a brain or detaching the
provider removes its transient diagnostic data. Reports describe the last
observed decisions, not a history of every action or continuous target tracking.

The native AI tests cover live rule changes, provider removal and 100/1000 idle
brains. Their printed frame timings measure the complete scene update for that
idle workload, both without combatants and with friendly combatants running
perception. They do not establish an attacking or pathfinding workload budget.

## Script API stability

`kb_cli api-check --baseline tests/api_baseline/script_api.json` compares the script API (functions, pins, components) with the committed baseline.
A removed function or a changed pin is a breaking change and fails with exit code 1, in CI and in the `kb_cli_api_check_committed_baseline` test.
An added function or component is reported as additive and passes. The baseline changes only on purpose: run the same command with `--update-baseline` and commit the file together with the change that needs it.

Project descriptors, project/scene metadata and particle effects reject newer file
versions with the stored and supported versions and an instruction to use a newer
engine. A zero or malformed binary header remains a corruption error. Loading or
refusing a file does not rewrite it. Older supported project descriptors retain
their settings for migration; stored older scenes and legacy particle effects are
checked by the native project/scene and particle asset test suites. API checks do
not replace these format and runtime checks.

## Scripting `kb_cli` without a person at the keyboard

Every command runs without prompts: all input comes from its options, and `kb_cli help`
lists them. Two things make it safe to drive from a script or an agent.

**Exit codes.** `0` means success. `1` means any failure: bad or missing arguments, an
unknown command, a check, build or run that failed, a file that could not be read or written.
The scene runner also returns `124` on timeout and `130` on cancellation.
`help`, `--help`, `-h` and a bare `kb_cli` print the usage and exit 0.

**`--json`.** Add it to any command and it prints one JSON object on stdout instead of text:

```json
{"schema":"21kb.cli-result/v1","ok":false,"exitCode":1,"command":"validate",
 "output":"OK   Assets/Logic/Player.lua\n","errors":["FAIL Assets/Logic/Broken.lua: ..."]}
```

- `ok` is `exitCode == 0`, and `exitCode` is also the process exit code.
- `output` is what the command printed on stdout; `errors` has one entry per line it wrote on
  stderr (without the `error: ` prefix). On success `errors` may still hold warnings.
- A command that prints a JSON object of its own (`audio-mixer ... --json`, `run --report ...`,
  `api --print json`) keeps it unchanged and is not wrapped; its failures that print nothing
  on stdout still come as the object above.
- The whole output is collected first, so a long command prints nothing until it ends.
  `mcp` ignores `--json`; it speaks JSON-RPC on stdout.
  JSON text replaces malformed UTF-8, including surrogate encodings and overlong
  sequences, with the Unicode replacement character.
- Native C++ and generated graph compiler logs stream to process stderr. They do not enter
  the JSON or MCP protocol stream; the structured result retains the build exit code and
  actionable error. This also applies when scene startup builds a native behaviour.

MCP validates arguments against its advertised tool schemas before dispatch. Unknown
properties, empty required strings, wrong types, fractional frame counts and integer
overflow are refused as tool errors. `run_scene` recording paths are confined to the
bound project; relative paths are anchored there even when the output file is new.
These path checks do not sandbox code executed by a trusted project.

CLI dispatch and MCP discovery use one command catalog. `kb_cli commands` returns
`21kb.cli-commands/v1`; a null `mcpTool` means that operation is available only
through the CLI, and `available` reflects the current build's capabilities.
The server retains its original tools and also exposes `component_schema`,
`scene_create`, `script_create`, `entity_edit` and `component_edit`. Authoring tools
use the bound project and invoke the same command handlers and save validation as
the CLI. For example, `component_edit` takes `action: "set"`, `entity`, `type`,
an optional `scene`, and `set: ["localPosition=3,4,5", "localScale=2,2,2"]`.
Discovery advertises the accepted action values and property types; the server
does not accept a project override or an arbitrary command line.

**Several changes as one transaction.** `batch` runs a list of commands so that all of them
take effect or none does:

```
kb_cli batch --project <dir> --file changes.txt [--dry-run] [--json]
```

`changes.txt` has one command per line (`#` starts a comment, quotes keep spaces together):

```
entity add --name Hero --position 0,1,0
component add --entity Hero --type Light --set intensity=2
script-new --name HeroLogic --language lua
```

A JSON file (an array of operations, each an array of words) works too. `--project` is added
to every line. If an operation fails, every file of the project is put back as it was and the
exit code is 1; the failing operation is named in the output. With `--dry-run` the operations
run the same way and are then undone, so you see what would change without writing anything:
each file as added, modified or deleted, with a short text diff (for a scene, the nodes and
their components that differ). `--json` prints `21kb.batch/v1` with `operations`, `changes`
(`path`, `status`, `bytesBefore`, `bytesAfter`, `diff`), `applied`, `rolledBack` and
`failedOperation`.

The transaction covers the files of the project folder except `Binaries`, `Intermediate`,
`Saved`, `Library`, `Build` and `.git`; files an operation writes elsewhere are not restored.
A project over 512 MiB is refused, and `batch` and `mcp` cannot be used inside a batch.
The snapshot must be complete: unreadable entries, symbolic links and special files
outside the excluded folders are refused before execution. Rollback restores both
file contents and directory structure, including file/directory replacements and
empty directories. A failed restoration keeps the backup and reports its location;
it does not report a successful rollback. Run batches without concurrent project
writers. This is recovery from command failures, not isolation from other processes
or protection against process termination during a write.

## Good to know

- Headless runs have no keyboard/gamepad — the agent tests logic; you test controls in
  the editor with Play.
- After updating the engine, run `kb_cli api --project .` to refresh the API reference.
- Use PowerShell or cmd for these commands; Git Bash mangles `/Game/...` paths.

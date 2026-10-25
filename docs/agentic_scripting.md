# AI-assisted scripting — quick guide

You open your game project in VS Code, write a prompt like *"create third-person player
movement and attach a follow camera"*, and the AI agent writes the Lua scripts, wires
them into the scene, and checks that they work. The engine ships a small tool,
`kb_cli`, that makes this possible.

## Setup

**Once per machine** — build the tool and add it to `PATH`:

```powershell
cmake --build build --config Debug --target kb_cli
```

The binary lands in `build\bin\Debug\kb_cli.exe`.

**Once per project** — prepare the project for agents:

```powershell
kb_cli init-agent --project H:\MyGame
```

This adds `AGENTS.md` (instructions the agent reads automatically) and an API reference
with editor autocompletion support.

**In VS Code:**

1. Install the **Lua** extension — you get autocompletion for the engine API.
2. Connect the engine to your MCP-compatible agent:

```powershell
<agent-cli> mcp add kb-engine -- <path>\kb_cli.exe mcp --project H:\MyGame
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
| `kb_cli batch --project <dir> --file <list> [--dry-run]` | run many commands as one all-or-nothing transaction, or preview their changes |

## Scripting `kb_cli` without a person at the keyboard

Every command runs without prompts: all input comes from its options, and `kb_cli help`
lists them. Two things make it safe to drive from a script or an agent.

**Exit codes.** `0` means success. `1` means any failure: bad or missing arguments, an
unknown command, a check, build or run that failed, a file that could not be read or written.
No other code is returned. `help`, `--help`, `-h` and a bare `kb_cli` print the usage and exit 0.

**`--json`.** Add it to any command and it prints one JSON object on stdout instead of text:

```json
{"schema":"21kb.cli-result/v1","ok":false,"exitCode":1,"command":"validate",
 "output":"OK   Assets/Logic/Player.lua
","errors":["FAIL Assets/Logic/Broken.lua: ..."]}
```

- `ok` is `exitCode == 0`, and `exitCode` is also the process exit code.
- `output` is what the command printed on stdout; `errors` has one entry per line it wrote on
  stderr (without the `error: ` prefix). On success `errors` may still hold warnings.
- A command that prints a JSON object of its own (`audio-mixer ... --json`, `run --report ...`,
  `api --print json`) keeps it unchanged and is not wrapped; its failures that print nothing
  on stdout still come as the object above.
- The whole output is collected first, so a long command prints nothing until it ends.
  `mcp` ignores `--json`; it speaks JSON-RPC on stdout.

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

## Good to know

- Headless runs have no keyboard/gamepad — the agent tests logic; you test controls in
  the editor with Play.
- After updating the engine, run `kb_cli api --project .` to refresh the API reference.
- Use PowerShell or cmd for these commands; Git Bash mangles `/Game/...` paths.

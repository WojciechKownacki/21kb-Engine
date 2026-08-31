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
| `kb_cli validate <file.lua>` | check a script for errors |
| `kb_cli scene-list --project <dir> --scene <path>` | show what's in a scene |
| `kb_cli scene-attach --project <dir> --scene <path> --node <name> --script <path>` | attach a script to a node |
| `kb_cli run --project <dir> --scene <path> --frames <n>` | run a scene without the editor and report errors |

## Good to know

- Headless runs have no keyboard/gamepad — the agent tests logic; you test controls in
  the editor with Play.
- After updating the engine, run `kb_cli api --project .` to refresh the API reference.
- Use PowerShell or cmd for these commands; Git Bash mangles `/Game/...` paths.

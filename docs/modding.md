# Modding

A mod adds content and scripts to a packaged game without touching the game's own files. It is a chunk pack (assets, scenes and Lua scripts) with a small manifest, installed in a `Mods` folder beside the game, signed by its author, and checked every time the game starts.

## What a mod is, and is not

- A mod is **content**: scenes, scripts, textures, audio, data. It adds assets next to the game's; it cannot replace one the game or another mod has, and it cannot change project settings.
- Its scripts run in the **same Lua sandbox** as the game's own scripts.
- It cannot bring **native code**. The manifest has no way to name a library, a plugin or an executable; a manifest with a key such as `plugin`, `dll`, `library`, `module`, `native`, `executable` or `binary` is refused by name. Native plugins are part of the game's own release and are verified by the release manifest, never loaded from `Mods`.
- A mod is cooked **against one build of the game**. Its pack names the catalogue identity of the game's base pack; a mod made for another build is refused (`BaseMismatch`). Rebuild the mod when the game's content changes.

## Layout

```
Game.exe  Game.kbpack  [Game.kbpackset ...]
Mods/
  Mods.state            21kb-mod-state 1, then "enabled <id>" lines: the enabled mods, in load order
  Mods.trust            21kb-mod-trust 1, then "allow-unsigned" and "signer <public key>" lines
  acme.crates/
    Mod.kbmod           the manifest
    acme.crates.kbpack  the chunk pack
```

## The manifest

```
21kb-mod 1
id acme.crates
name Crate Pack
version 1.2.0
author Acme
description More crates for the warehouse.
api ^1.0.0
requires acme.core >=1.0.0 <2.0.0
conflicts acme.oldcrates
entry /Game/Scenes/Crates.21kbscene
pack acme.crates.kbpack
pack-sha512 <128 hexadecimal digits>
signer <64 hexadecimal digits>
```

- `id`: lower case letters, digits, `.`, `_`, `-`, at most 48 characters. It is the mod's folder, and the label of its pack.
- `version`: `major.minor.patch`. Version ranges are space-separated comparators that must all hold: `>=1.2.0 <2.0.0`, `^1.2.0` (same major), `~1.2.0` (same minor), `1.2.3` (exactly), `*` (any).
- `api`: the mod interface versions the mod was written for. This engine offers `1.0.0`.
- `requires` / `conflicts`: other mods by id. A required mod must be installed, enabled, loadable and in range; of two conflicting mods the earlier one in load order loads and the other is refused.
- `entry`: a scene the game may open for this mod.
- `pack-sha512`, `signer`: written by `kb_cli mod build`; they bind the manifest to its pack.

## Making a mod

The mod author works in a copy of the game's project that adds content, cooks it like any project, and builds the mod from that cook:

```
kb_cooker --project MyMod --target Windows.x64 --output MyMod.kbpack ...
kb_cli keys generate --out author.key            (once; keep it private)
kb_cli mod build --game <Game.kbpackset | Game.kbpack> --cook MyMod.kbpack \
    --id acme.crates --version 1.0.0 --name "Crate Pack" --entry /Game/Scenes/Crates.21kbscene \
    --requires acme.core@>=1.0.0,<2.0.0 --key author.key --output out/acme.crates
```

`mod build` writes a chunk pack with the assets of the cook that the game does not already have (those it has are left out and counted), seals it with the author's key, and writes `Mod.kbmod`. For a game whose packs are encrypted, `--current-release <release dir>` reads them the way that release's player does.

## Installing and managing

```
kb_cli mod install  --mods <Mods> [--game <Game pack>] [--replace] <mod folder>
kb_cli mod list     --mods <Mods> [--game <Game pack>]
kb_cli mod verify   --mods <Mods> --game <Game pack>      what would load, what is refused and why
kb_cli mod disable|enable|remove --mods <Mods> <id>
kb_cli mod trust    --mods <Mods> [--add <public key>] [--remove <public key>] [--allow-unsigned|--deny-unsigned]
kb_cli mod info     [--game <Game pack>] <mod folder>
```

Install checks everything below before it copies a byte into `Mods` and verifies the copy; a mod that does not pass leaves nothing behind. Enabling puts a mod last in load order; disabling keeps the order of the others.

## What makes a mod load

A mod loads when all of these hold, and is otherwise refused with its reason while the other mods and the game carry on:

1. The manifest parses, names no native code, and the folder is the mod's id.
2. The pack is the one the manifest hashes (SHA-512) and mounts as a chunk pack labelled with the mod's id; it was cooked against this build of the game; its `api` range accepts this engine.
3. **Trust.** The pack is sealed by a key the player trusts (`Mods.trust`), or, only if the player allows it, not sealed at all. A seal proves the content is what its author sealed and that nothing in the catalogue changed; the trust list says whose seal counts. The manifest's `signer` must equal the key that sealed the pack, so a stripped or swapped seal does not pass.
4. Its required mods load and match; no conflicting mod ahead of it loads.
5. The runtime accepts the pack beside the game's: an asset that another pack has (other than a byte-identical shared one) refuses the mod, and the refusal names it.

Trust of mods is separate from trust of the game. A shipped game requires its release key on every pack of its own; a mod pack is read under the key it was sealed with (`RuntimeAssetPackMount::trust`), after the game itself has been verified against its signed release manifest and without touching that verification. A player without a release key (a development build) accepts unsigned mods when there is no `Mods.trust` file; a trust file always takes over. Unsigned mods never load in a player whose trust file does not say `allow-unsigned`.

## In the game

The player mounts the enabled, loadable mods at start (desktop players; the web and Android hosts do not read a `Mods` folder) and logs `mod loaded: <id> <version>` and `mod refused: <id> <version>: <problem> (<detail>)`. Scripts see what loaded:

```lua
local count = CallFunction("Mods.Count", {})
for index = 0, count - 1 do
    local mod = CallFunction("Mods.Get", { index = index })      -- id, name, version, author, entry
    if mod.entry ~= "" then CallFunction("Scene.Load", { path = mod.entry, additive = true }) end
end
-- CallFunction("Mods.IsLoaded", { id = "acme.crates" })  -> loaded, version
-- CallFunction("Mods.EntryScene", { id = "acme.crates" }) -> scene
```

The game decides what a mod's content is for. A mod's own scripts run when its scenes are open, like any script.

## Tests

- `kb_engine_tests mods`: versions and ranges, the manifest (every refusal, the native-code keys, round trip), the trust file, and the catalogue on real sealed chunk packs: clean, unsigned, untrusted, stripped seal, swapped seal, changed header, changed payload, truncated, garbage, missing, wrong role or label, newer API, other build, wrong folder, native code; install, replace, remove, enable and disable; dependency order, version, missing, disabled and refused dependencies, conflicts, cycles.
- `kb_game_mods`: a game sealed by a release key with mods under their own keys: each pack served by its own container, the release key still decisive for the game's packs, a chunk that replaces a game asset refused by name while the other mod loads, a mod from another build refused.
- `kb_game_mods_package`: the whole flow in the packaged player: two mods where one needs the other (order, both scripts run), disable, remove (the dependant refused with its reason), unsigned and untrusted mods under the trust file, a damaged and a truncated pack, a mod for a newer interface, one built for another build of the game, one that names a native library.

## Limits

- A mod is bound to one build of the game by the base pack's identity; there is no cross-build compatibility range.
- The manifest's text (dependencies, version) is not itself signed; the seal covers the pack, and the manifest is bound to the pack by its hash and signer. A mod whose manifest was edited keeps its content but may declare other dependencies.
- Mods add assets only: no replacing a game asset, no settings, no tombstones. Overriding the game would need a patch-style mod with its own review.
- Checked on Windows only. The mounting is in the shared desktop bootstrap (`GameProjectRuntime`); the web and Android hosts mount their packs themselves and do not read a `Mods` folder.
- No hot reload: mods are mounted at start.
- The Lua sandbox of the game applies to mod scripts; resource limits (time, memory) of a misbehaving mod script are the engine's general script limits, with no per-mod budget.

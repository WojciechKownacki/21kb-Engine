"""Mods of a packaged game: built from a cook, installed, disabled, removed and refused when they should be, in the real player.

A base game (a script that lists the mods the player loaded and opens each one's entry scene) is cooked and staged beside the player. Mods
are built with `kb_cli mod build` from cooks of extended copies of the project, installed into the game's Mods folder with `kb_cli mod`, and the
player is run after every change. The cases: two signed mods where one needs the other (load order, both scripts run), disabling and removing
(the dependant is refused with its reason), an unsigned mod under the default and the permissive trust file, a damaged pack, a mod built for
another build of the game, a mod for a newer mod interface, a mod that names a native library, and a mod that tries to replace a game asset.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


LOADER = """\
function Ready(self, dt)
    local count, problem = CallFunction("Mods.Count", {})
    if count == nil then
        Log("Mods.Count failed: " .. tostring(problem))
        return
    end
    Log("base game: mods=" .. tostring(count))
    for index = 0, count - 1 do
        local mod = CallFunction("Mods.Get", { index = index })
        Log("base sees mod " .. mod.id .. " " .. mod.version)
        if mod.entry ~= "" then
            CallFunction("Scene.Load", { path = mod.entry, additive = true })
        end
    end
end
"""


def hello(text):
    return f'function Ready(self, dt)\n    Log("{text}")\nend\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--plugin", action="append", type=Path, required=True)
    parser.add_argument("--runtime-dll", action="append", type=Path, default=[])
    args = parser.parse_args()
    for name in ["cli", "cooker", "player", "engine_root", "test_root", "shaderc"]:
        setattr(args, name, getattr(args, name).resolve())
    args.plugin = [path.resolve() for path in args.plugin]
    args.runtime_dll = [path.resolve() for path in args.runtime_dll]
    require(os.name == "nt", "The mod test uses the native Windows player")
    ctypes.windll.kernel32.SetErrorMode(32771)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="Mods", dir=args.test_root))
    temp = root / "Temp"
    temp.mkdir()
    env = dict(os.environ, TEMP=str(temp), TMP=str(temp), MSYS_NO_PATHCONV="1")
    history = []

    def run(exe, *tail, expected=0, cwd=root, timeout=180):
        start = time.monotonic()
        process = subprocess.Popen([str(exe), *map(str, tail)], cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True,
                           creationflags=subprocess.CREATE_NO_WINDOW, check=False)
            process.communicate()
            raise
        row = dict(executable=str(exe), arguments=list(map(str, tail)), code=process.returncode, seconds=time.monotonic() - start,
                   stdout=stdout.decode("utf8", "replace"), stderr=stderr.decode("utf8", "replace"))
        history.append(row)
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf8")
        require(process.returncode == expected, f"Unexpected command result: {row}")
        return row["stdout"] + row["stderr"]

    def cli(*tail, **kwargs):
        return run(args.cli, *tail, **kwargs)

    def cook(project, output):
        output.parent.mkdir(parents=True, exist_ok=True)
        run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", output, "--engine-root", args.engine_root,
            "--shaderc", args.shaderc, "--cache", root / "CookCache")

    # ---- The game: a script that lists the loaded mods and opens each one's entry scene ----
    game = root / "Game"
    cli("new", "--parent", root, "--name", "Game", "--template", "Empty")
    cli("entity", "add", "--project", game, "--name", "Camera", "--create", "Camera")
    cli("script-new", "--project", game, "--name", "Loader", "--language", "lua")
    (game / "Assets/Scripts/Loader.lua").write_text(LOADER, encoding="utf8")
    cli("scene-attach", "--project", game, "--scene", "Assets/Scenes/Main.21kbscene", "--node", "Camera", "--script", "/Game/Scripts/Loader.lua")
    base_pack = root / "Base" / "Game.kbpack"
    cook(game, base_pack)

    def extended(name, scene, text):
        """A copy of the game's project with one scene and one script more, which is the default scene so that the cook holds them."""
        project = root / name
        shutil.copytree(game, project, ignore=shutil.ignore_patterns("Saved", ".kb-cook-cache"))
        cli("scene-new", "--project", project, "--name", scene, "--template", "Empty", "--default", "yes")
        cli("entity", "add", "--project", project, "--scene", f"Assets/Scenes/{scene}.21kbscene", "--name", "Marker", "--create", "Cube")
        cli("script-new", "--project", project, "--name", f"{scene}Hello", "--language", "lua")
        (project / f"Assets/Scripts/{scene}Hello.lua").write_text(hello(text), encoding="utf8")
        cli("scene-attach", "--project", project, "--scene", f"Assets/Scenes/{scene}.21kbscene", "--node", "Marker", "--script",
            f"/Game/Scripts/{scene}Hello.lua")
        cooked = root / "Cooks" / f"{name}.kbpack"
        cook(project, cooked)
        return cooked

    # Private keys are never written inside a repository, so they live in the system temporary folder, outside the test root.
    keys = Path(tempfile.mkdtemp(prefix="ModKeys"))
    author = keys / "author.key"
    cli("keys", "generate", "--out", author)
    author_public = cli("keys", "public", "--key", author).strip().splitlines()[-1]
    stranger = keys / "stranger.key"
    cli("keys", "generate", "--out", stranger)

    def build(mod_id, cooked, *extra, base=base_pack, expected=0):
        out = root / "ModBuilds" / mod_id
        text = cli("mod", "build", "--game", base, "--cook", cooked, "--id", mod_id, "--version", "1.0.0", "--output", out, *extra, expected=expected)
        return out, text

    crates_cook = extended("Crates", "CratesLevel", "crates mod script ran")
    crates, text = build("acme.crates", crates_cook, "--name", "Crate Pack", "--author", "Acme", "--entry", "/Game/Scenes/CratesLevel.21kbscene",
                         "--key", author)
    require("sealed" in text and "asset(s) added" in text, "The build did not report what it made: " + text)
    require((crates / "Mod.kbmod").exists() and (crates / "acme.crates.kbpack").exists(), "The build did not write the mod")
    manifest = (crates / "Mod.kbmod").read_text(encoding="utf8")
    require("id acme.crates" in manifest and f"signer {author_public}" in manifest and "pack-sha512 " in manifest, "The manifest is incomplete: " + manifest)

    extras_cook = extended("Extras", "ExtrasLevel", "extras mod script ran")
    extras, _ = build("acme.extras", extras_cook, "--entry", "/Game/Scenes/ExtrasLevel.21kbscene", "--requires", "acme.crates@>=1.0.0,<2.0.0", "--key", author)

    # ---- The packaged game ----
    package = root / "Packaged"
    package.mkdir()
    player = package / "Game.exe"
    for source_file in [args.player, *args.plugin, *args.runtime_dll]:
        shutil.copy2(source_file, player if source_file == args.player else package / source_file.name)
    shutil.copy2(base_pack, package / "Game.kbpack")
    mods = package / "Mods"
    game_pack = package / "Game.kbpack"

    def play():
        return run(player, "--headless", "--frames=8", "--width=64", "--height=64", "--uncapped", cwd=package)

    def mod(*tail, **kwargs):
        return cli("mod", *tail, "--mods", mods, **kwargs)

    text = play()
    require("base game: mods=0" in text and "shutdown=clean" in text, "The game without mods did not run: " + text)

    # The default trust of a player with no trust file is a development player's: signed mods and unsigned ones load. A real player is stricter:
    # a trust file names whom to believe.
    mod("trust", "--add", author_public)
    require("Mods.trust" in str(list(mods.iterdir())) , "The trust file was not written")
    text = mod("install", "--game", game_pack, str(crates))
    require("installed acme.crates" in text, "Installing a signed mod failed: " + text)
    text = mod("install", "--game", game_pack, str(extras))
    require("installed acme.extras" in text, "Installing the dependant failed: " + text)
    listing = mod("list", "--game", game_pack)
    require("[x] acme.crates" in listing and "[x] acme.extras" in listing and "2 enabled" in listing, "The list is wrong: " + listing)

    text = play()
    require("base game: mods=2" in text and "mod loaded: acme.crates 1.0.0" in text and "mod loaded: acme.extras 1.0.0" in text and "shutdown=clean" in text,
            "The player did not load both mods: " + text)
    require(text.index("base sees mod acme.crates") < text.index("base sees mod acme.extras"), "The mods are not listed dependencies first")
    require("crates mod script ran" in text and "extras mod script ran" in text, "A mod's script did not run in the packaged player: " + text)

    # Disabling one mod: the other still loads.
    mod("disable", "acme.extras")
    text = play()
    require("base game: mods=1" in text and "crates mod script ran" in text and "extras mod script ran" not in text, "A disabled mod ran: " + text)
    mod("enable", "acme.extras")
    # Removing the mod another needs: the dependant is refused with the reason, the game runs.
    mod("remove", "acme.crates")
    text = play()
    require("base game: mods=0" in text and "mod refused: acme.extras 1.0.0: DependencyMissing" in text and "shutdown=clean" in text,
            "A mod whose dependency was removed was not refused: " + text)
    verify = mod("verify", "--game", game_pack, expected=1)
    require("refused" in verify and "DependencyMissing" in verify, "verify did not name the missing dependency: " + verify)
    mod("install", "--game", game_pack, str(crates))
    text = play()
    require("base game: mods=2" in text, "Reinstalling the dependency did not bring the dependant back: " + text)

    # ---- Trust ----
    plain_cook = extended("Plain", "PlainLevel", "plain mod script ran")
    plain, _ = build("acme.plain", plain_cook, "--entry", "/Game/Scenes/PlainLevel.21kbscene")
    text = mod("install", "--game", game_pack, str(plain), expected=1)
    require("Unsigned" in text, "An unsigned mod installed under a trust file that does not allow it: " + text)
    strange, _ = build("acme.strange", plain_cook, "--entry", "/Game/Scenes/PlainLevel.21kbscene", "--key", stranger)
    text = mod("install", "--game", game_pack, str(strange), expected=1)
    require("UntrustedSigner" in text, "A mod sealed by a key nobody trusts installed: " + text)
    mod("trust", "--allow-unsigned")
    text = mod("install", "--game", game_pack, str(plain))
    require("installed acme.plain" in text, "An unsigned mod did not install when unsigned mods are allowed: " + text)
    text = play()
    require("plain mod script ran" in text and "base game: mods=3" in text, "An allowed unsigned mod did not run: " + text)
    mod("trust", "--deny-unsigned")
    text = play()
    require("mod refused: acme.plain 1.0.0: Unsigned" in text and "base game: mods=2" in text and "plain mod script ran" not in text,
            "An unsigned mod still ran after unsigned mods were refused: " + text)
    mod("remove", "acme.plain")

    # ---- Damage ----
    pack_file = mods / "acme.crates" / "acme.crates.kbpack"
    original = pack_file.read_bytes()
    damaged = bytearray(original)
    damaged[len(damaged) // 2] ^= 0x55
    pack_file.write_bytes(bytes(damaged))
    text = play()
    require("mod refused: acme.crates 1.0.0" in text and "extras mod script ran" not in text and "shutdown=clean" in text,
            "A mod pack with a changed byte loaded: " + text)
    require("DependencyRefused" in text, "The mod that needs the damaged one was not refused: " + text)
    pack_file.write_bytes(original[: len(original) // 2])
    text = play()
    require("mod refused: acme.crates 1.0.0" in text and "shutdown=clean" in text, "A truncated mod pack loaded: " + text)
    pack_file.write_bytes(original)
    text = play()
    require("base game: mods=2" in text, "Restoring the pack did not bring the mods back: " + text)

    # ---- Compatibility ----
    newer, _ = build("acme.newer", plain_cook, "--api", ">=2.0.0", "--key", author)
    text = mod("install", "--game", game_pack, str(newer), expected=1)
    require("ApiIncompatible" in text, "A mod for a newer mod interface installed: " + text)

    # A mod built against another build of the game (one more entity in the base) is refused by this one.
    other = root / "Other"
    shutil.copytree(game, other, ignore=shutil.ignore_patterns("Saved", ".kb-cook-cache"))
    cli("entity", "add", "--project", other, "--name", "Extra", "--create", "Cube")
    other_pack = root / "OtherBase" / "Game.kbpack"
    cook(other, other_pack)
    foreign_cook = extended("Foreign", "ForeignLevel", "foreign mod script ran")
    foreign, _ = build("acme.foreign", foreign_cook, "--key", author, base=other_pack)
    text = mod("install", "--game", game_pack, str(foreign), expected=1)
    require("BaseMismatch" in text, "A mod built against another build of the game installed: " + text)
    # Put there by hand, past the installer, the player still refuses it and runs.
    shutil.copytree(foreign, mods / "acme.foreign")
    state = (mods / "Mods.state").read_text(encoding="utf8")
    (mods / "Mods.state").write_text(state + "enabled acme.foreign\n", encoding="utf8")
    text = play()
    require("mod refused: acme.foreign 1.0.0: BaseMismatch" in text and "foreign mod script ran" not in text and "base game: mods=2" in text,
            "A mod for another build was not refused by the player: " + text)
    mod("remove", "acme.foreign")

    # ---- Native code ----
    native = root / "ModBuilds" / "acme.native"
    shutil.copytree(crates, native)
    shutil.move(native / "Mod.kbmod", native / "Mod.old")
    manifest = (native / "Mod.old").read_text(encoding="utf8").replace("id acme.crates", "id acme.native")
    (native / "Mod.kbmod").write_text(manifest + "plugin Evil.dll\n", encoding="utf8")
    (native / "Mod.old").unlink()
    text = mod("install", "--game", game_pack, str(native), expected=1)
    require("native code" in text, "A manifest naming a native library was not refused with the reason: " + text)

    # ---- A mod cannot replace the game: the builder leaves out the assets the game has (the runtime refusing a chunk that holds one anyway is in kb_game_core_tests --mods) ----
    require("of the game's own left out" in cli("mod", "build", "--game", base_pack, "--cook", crates_cook, "--id", "acme.again", "--version", "1.0.0",
                                              "--output", root / "ModBuilds" / "acme.again"), "The build did not leave out the game's own assets")

    (root / "result.json").write_text(json.dumps(dict(result="PASS", limitation="Windows player, development trust (no release key): the release-key "
        "path is covered by the in-process test; mods are bound to one build of the game."), indent=2), encoding="utf8")
    shutil.rmtree(keys, ignore_errors=True)
    print(f"PASS: mods built, installed, disabled, removed and refused in the packaged player; {root}")


if __name__ == "__main__":
    main()

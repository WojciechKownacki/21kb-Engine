"""Check touch query availability through authoring, cook and the hidden packaged player."""

import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ["cli", "cooker", "player", "engine-root", "test-root", "shaderc"]:
        parser.add_argument("--" + option, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", required=True)
    parser.add_argument("--runtime-dll", type=Path, action="append", default=[])
    args = parser.parse_args()
    require(os.name == "nt", "The packaged input test requires the native Windows host")
    require(args.shaderc.is_file(), "Packaged input testing requires shaderc; enable KB_BUILD_GRAPH_SHADERC or set KB_COOKER_SHADERC")
    ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="TouchScriptPackage", dir=args.test_root))
    project = root / "Sample"
    history = []

    def run(executable, *tail, expected=0, cwd=root):
        process = subprocess.run([str(executable), *map(str, tail)], cwd=cwd, capture_output=True,
                                 text=True, encoding="utf-8", errors="replace", timeout=150,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(executable=str(executable), arguments=list(map(str, tail)),
                            code=process.returncode, stdout=process.stdout, stderr=process.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        require(process.returncode == expected, f"Unexpected exit {process.returncode}: {history[-1]}")
        return process.stdout + process.stderr

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "InputProbe")
    run(args.cli, "script-new", "--project", project, "--name", "TouchProbe", "--language", "lua")
    script = project / "Assets/Scripts/TouchProbe.lua"
    script.write_text('''function Ready(self, dt)
    assert(Input.TouchCount() == 0, "packaged touch count")
    local point = Input.TouchPoint(0)
    assert(not point.valid and not point.active and point.id == 0 and point.phase == 3,
           "packaged inactive touch point")
    local ok, message = pcall(Input.TouchPoint, 4294967296)
    assert(not ok and string.find(message, "index must fit an Int", 1, true),
           "packaged index overflow diagnostic")
    assert(World.Spawn("TouchQueriesVerified") ~= nil)
    Log("packaged touch queries ready")
    Log("runtime Unicode: Ω日")
end
function Tick(self, dt)
    assert(Input.TouchCount() == 0 and not Input.TouchPoint(-1).valid,
           "packaged touch queries remain callable")
end
''', encoding="utf-8")
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "InputProbe", "--script", "/Game/Scripts/TouchProbe.lua")

    def package(name):
        output = root / name
        output.mkdir()
        run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", output / "Game.kbpack",
            "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
        player = output / "Sample.exe"
        shutil.copy2(args.player, player)
        for dependency in [*args.plugin, *args.runtime_dll]:
            shutil.copy2(dependency, output / dependency.name)
        require(not (output / "Assets").exists() and not (output / "Source").exists(), "Package contains loose project content")
        return player

    player = package("Valid")
    output = run(player, "--headless", "--frames=3", "--width=64", "--height=64", cwd=player.parent)
    require("frames=3" in output and "rendered=3" in output and "ticks=3" in output and
            "shutdown=clean" in output, "Packaged script did not execute, draw and finish three frames")
    require("kb_game: script: packaged touch queries ready" in output, "Packaged script log did not reach the host output")
    require("kb_game: script: runtime Unicode: Ω日" in output, "Packaged script log lost UTF-8 text")
    backend = re.search(r" backend=(.+?) resolution=", output)
    require(backend is not None and backend[1].lower() != "noop", "Packaged player did not select a rendering backend")

    script.write_text('''function Ready(self, dt)
    assert(Input.TouchCount() == 1, "touch negative contact control")
end
''', encoding="utf-8")
    rejected = package("NegativeControl")
    output = run(rejected, "--headless", "--frames=3", "--width=64", "--height=64",
                 cwd=rejected.parent, expected=1)
    require("touch negative contact control" in output and "script error" in output,
            "Packaged negative control did not report its real script assertion")
    report = dict(result="PASS", backend=backend[1], packagedFrames=3, nativeTouchContacts=0,
                  negativeScriptExit=1,
                  limitation="Package checks query availability and the empty snapshot; independent multi-contact semantics run in the engine script suite. This is not browser joystick/UI acceptance.")
    (root / "result.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: cooked touch query script and negative control in actual packaged player; {root}")


if __name__ == "__main__":
    main()

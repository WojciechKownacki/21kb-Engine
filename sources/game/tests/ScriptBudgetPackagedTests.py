"""Verify default Lua instruction limits through authoring and a cooked player."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "player", "engine-root", "shaderc", "test-root"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", required=True)
    parser.add_argument("--runtime-dll", type=Path, action="append", default=[])
    args = parser.parse_args()
    assert os.name == "nt", "This package test requires the Windows host"
    ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="ScriptBudget", dir=args.test_root))
    project, package = root / "Sample", root / "Package"
    history = []

    def run(executable, *tail, expected=0, cwd=root):
        command = list(map(str, [executable, *tail]))
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=60,
            creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(command=command, exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    # Validation must not hang on code executed while a script is loading.
    run(args.cli, "script-new", "--project", project, "--name", "LoadLoop", "--language", "lua")
    load_loop = project / "Assets/Scripts/LoadLoop.lua"
    load_loop.write_text("while true do end\n", encoding="utf-8")
    refused = run(args.cli, "validate", "--project", project, load_loop, expected=1)
    assert "execution budget exceeded" in refused
    load_loop.write_text("while true do pcall(function() while true do end end) end\n", encoding="utf-8")
    refused = run(args.cli, "validate", "--project", project, load_loop, expected=1)
    assert "execution budget exceeded" in refused
    load_loop.write_text("function Tick(self, dt) end\n", encoding="utf-8")
    run(args.cli, "validate", "--project", project, load_loop)
    sources = {
        "DirectLoop": "function Tick(self, dt) while true do end end\n",
        "CoroutineLoop": "function Tick(self, dt) coroutine.wrap(function() while true do end end)() end\n",
        "CaughtLoop": "function Tick(self, dt) while true do pcall(function() while true do end end) end end\n",
        "Healthy": "local ticks=0\nfunction Tick(self, dt) ticks=ticks+1 Log('healthy tick '..ticks) end\n",
    }
    for name, source in sources.items():
        run(args.cli, "entity", "add", "--project", project, "--name", name)
        run(args.cli, "script-new", "--project", project, "--name", name, "--language", "lua")
        (project / "Assets/Scripts" / (name + ".lua")).write_text(source, encoding="utf-8")
        run(args.cli, "scene-attach", "--project", project, "--scene", "/Game/Scenes/Main.21kbscene",
            "--node", name, "--script", "/Game/Scripts/" + name + ".lua")
    package.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
        "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
    shutil.copy2(args.player, package / "Sample.exe")
    for dependency in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(dependency, package / dependency.name)
    project.rename(root / "SourceUnavailable")
    output = run(package / "Sample.exe", "--headless", "--frames=6", "--width=64", "--height=64",
        cwd=package, expected=1)
    assert "execution budget exceeded" in output and all(name + ".lua" in output
        for name in ["DirectLoop", "CoroutineLoop", "CaughtLoop"]), output
    assert "healthy tick 6" in output and "rendered=6" in output and "shutdown=clean" in output, output
    report = dict(result="PASS", loadLoopRefused=True, correctedScriptAccepted=True,
        directLoopRefused=True, coroutineLoopRefused=True, protectedLoopRefused=True, healthyTicks=6, renderedFrames=6,
        sourceUnavailable=True)
    (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(**report, artifacts=str(root))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

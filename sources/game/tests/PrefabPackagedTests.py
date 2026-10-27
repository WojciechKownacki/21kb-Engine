"""Verify declared Lua prefab dependencies survive cooking without project sources."""
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
    root = Path(tempfile.mkdtemp(prefix="PrefabPackage", dir=args.test_root))
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
    prefabs = project / "Assets/Prefabs"
    prefabs.mkdir(exist_ok=True)
    shutil.copy2(args.engine_root / "sources/editor/tests/fixtures/prefab/LegacyCrate.kbprefab", prefabs / "Crate.kbprefab")
    run(args.cli, "script-new", "--project", project, "--name", "Probe", "--language", "lua")
    source = """
-- @asset /Game/Prefabs/Crate.kbprefab
-- @asset /Game/Prefabs/Crate.kbprefab
local ignored = [[
-- @asset /Game/NotADeclaration.kbprefab
]]
local ticks = 0
function Ready(self, dt)
    local entity, diagnostic = CallFunction("World.Spawn", {prefab="/Game/Prefabs/Crate.kbprefab", x=7, y=2, z=3})
    assert(entity ~= nil and entity ~= 0, diagnostic)
    assert(World.Name(entity) == "LegacyCrate")
end
function Tick(self, dt)
    ticks = ticks + 1
    if ticks < 2 then return end
    for _, name in ipairs({"Lid", "Hinge", "Handle"}) do
        local child = World.FindByName(name)
        assert(child ~= 0, name)
        local pose = CallFunction("Transform.WorldPose", {entity=child})
        assert(pose.found and math.abs(pose.posX - 7) < 0.001 and math.abs(pose.posY - 2) < 0.001 and math.abs(pose.posZ - 3) < 0.001, name .. " pose " .. tostring(pose.posX) .. "," .. tostring(pose.posY) .. "," .. tostring(pose.posZ))
    end
    Log("packaged prefab hierarchy loaded")
end
"""
    (project / "Assets/Scripts/Probe.lua").write_text(source, encoding="utf-8")
    run(args.cli, "scene-attach", "--project", project, "--scene", "/Game/Scenes/Main.21kbscene",
        "--node", "MainCamera", "--script", "/Game/Scripts/Probe.lua")
    run(args.cli, "run", "--project", project, "--frames", "2")
    # Declared paths must fail at cook time rather than produce a broken runtime package.
    probe = project / "Assets/Scripts/Probe.lua"
    for bad in ["/Game/Prefabs/Missing.kbprefab", "../Prefabs/Crate.kbprefab", "/Game/Prefabs/../Prefabs/Crate.kbprefab"]:
        probe.write_text(source.replace("-- @asset /Game/Prefabs/Crate.kbprefab", "-- @asset " + bad), encoding="utf-8")
        refused = run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", root / "Refused.kbpack",
            "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache", expected=1)
        assert "Lua @asset" in refused, refused
    # Prefab GUID and stable node IDs are stored in the prefab itself.
    moved = project / "Assets/Prefabs/Moved"
    moved.mkdir()
    original = prefabs / "Crate.kbprefab"
    identity = original.read_bytes()
    original.rename(moved / "Renamed.kbprefab")
    assert (moved / "Renamed.kbprefab").read_bytes() == identity
    source = source.replace("/Game/Prefabs/Crate.kbprefab", "/Game/Prefabs/Moved/Renamed.kbprefab")
    probe.write_text(source, encoding="utf-8")
    run(args.cli, "run", "--project", project, "--frames", "2")

    package.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
        "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
    shutil.copy2(args.player, package / "Sample.exe")
    for dependency in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(dependency, package / dependency.name)
    project.rename(root / "SourceUnavailable")
    output = run(package / "Sample.exe", "--headless", "--frames=6", "--width=64", "--height=64", cwd=package)
    assert "packaged prefab hierarchy loaded" in output and "shutdown=clean" in output and "rendered=6" in output, output
    (root / "result.json").write_text(json.dumps(dict(result="PASS", invalidDependenciesRefused=3, migratedLegacyPrefab=True, movedPrefabIdentityPreserved=True, sourceUnavailable=True, children=3, renderedFrames=6), indent=2), encoding="utf-8")
    print(root)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

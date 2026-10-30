"""Verify cooked Jolt bodies, character traversal and scene lifecycle through public authoring APIs."""
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
    root = Path(tempfile.mkdtemp(prefix="PhysicsPackage", dir=args.test_root))
    project, package = root / "Sample", root / "Package"
    history = []

    def run(executable, *tail, expected=0, cwd=root):
        command = list(map(str, [executable, *tail]))
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=180,
            creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(command=command, exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    def cli(*tail):
        return run(args.cli, *tail, "--project", project)

    def entity(name, position, rotation=None):
        options = ["entity", "add", "--name", name, "--position", ",".join(map(str, position))]
        if rotation:
            options += ["--rotation", ",".join(map(str, rotation))]
        cli(*options)

    def component(name, kind, **values):
        options = ["component", "add", "--entity", name, "--type", kind]
        for key, value in values.items():
            options += ["--set", key + "=" + (str(value).lower() if isinstance(value, bool) else str(value))]
        cli(*options)

    def box(name, position, size, body_type="Static", rotation=None, **body):
        entity(name, position, rotation)
        component(name, "Collider", **{"boxSize": ",".join(map(str, size))})
        component(name, "Rigidbody", bodyType=body_type, **body)

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    cli("entity", "add", "--create", "Camera", "--name", "MainCamera")
    box("StairFloor", (5,-.5,0), (20,1,4))
    for i in range(3):
        height = .2 * (i+1)
        box("Step"+str(i), (2+i,height/2,0), (1,height,4))
    box("SlopeFloor", (4,-.5,10), (20,1,4))
    box("Slope", (4,1.25,10), (8,.5,4), rotation=(0,0,20))
    box("Platform", (0,-.5,20), (4,1,4), "Kinematic", useGravity=False)
    box("Edge", (0,-.5,30), (4,1,4))
    box("FastFloor", (5,-.5,40), (20,1,4))
    box("FastWall", (5,2,40), (.1,5,4))
    box("CcdWall", (5,0,50), (.1,10,4))
    entity("CcdBody", (0,0,50))
    component("CcdBody", "Collider", shape="Sphere", radius=.2)
    component("CcdBody", "Rigidbody", useGravity=False, useContinuousCollision=True, **{"linearVelocity.x":200})
    box("StackFloor", (0,-.5,60), (10,1,4))
    for i in range(4):
        box("Stack"+str(i), (0,.51+i*1.02,60), (1,1,1), "Dynamic")
    for name, position in [("Stairs",(0,.91,0)),("SlopeWalker",(0,2,10)),("Rider",(0,.91,20)),("EdgeWalker",(0,.91,30)),("FastWalker",(0,.91,40))]:
        entity(name,position)
        component(name,"CharacterController",radius=.3,height=1.8,stepOffset=.4,slopeLimitDegrees=50)
    cli("scene-new", "--name", "Reload", "--template", "Empty")
    cli("entity", "add", "--scene", "/Game/Scenes/Reload.21kbscene", "--name", "ReloadBody", "--position", "15,10,60")
    cli("component", "add", "--scene", "/Game/Scenes/Reload.21kbscene", "--entity", "ReloadBody", "--type", "Collider")
    cli("component", "add", "--scene", "/Game/Scenes/Reload.21kbscene", "--entity", "ReloadBody", "--type", "Rigidbody")
    cli("script-new", "--name", "Probe", "--language", "lua")
    source = r"""
-- @asset /Game/Scenes/Reload.21kbscene
local elapsed = 0
local clone = false
local loaded = nil
local phase = 0
local ids = {}
local function pose(name)
 return CallFunction("Transform.WorldPose", {entity=ids[name]})
end
local function move(name, speed)
 assert(CallFunction("Physics.CharacterMove", {entity=ids[name], moveX=speed, moveZ=0}), name .. " movement unavailable")
end
function Ready(self, dt)
 clone = GetShared("physicsPass") == true
 if clone then return end
 for _,name in ipairs({"Stairs","SlopeWalker","Rider","EdgeWalker","FastWalker","Platform","CcdBody","Stack0","Stack1","Stack2","Stack3"}) do
  ids[name] = World.FindByName(name)
  assert(ids[name] ~= 0, name .. " missing")
 end
end
function Tick(self, dt)
 if clone then return end
 elapsed = elapsed + dt
 if phase == 0 then
  move("Stairs", 1.5)
  move("SlopeWalker", 2)
  move("EdgeWalker", 3)
  move("FastWalker", 100)
  assert(CallFunction("Transform.SetPosition", {entity=ids.Platform,x=elapsed,y=-.5,z=20}))
  if elapsed < 3 then return end
  local stairs, slope, rider, edge, fast, ccd = pose("Stairs"),pose("SlopeWalker"),pose("Rider"),pose("EdgeWalker"),pose("FastWalker"),pose("CcdBody")
  Log(string.format("physics-positions stairs=%.3f,%.3f slope=%.3f,%.3f rider=%.3f,%.3f edge=%.3f,%.3f fast=%.3f ccd=%.3f",stairs.posX,stairs.posY,slope.posX,slope.posY,rider.posX,rider.posY,edge.posX,edge.posY,fast.posX,ccd.posX))
  assert(stairs.posX > 3.6 and stairs.posY > 1.1, "stairs traversal failed")
  assert(slope.posX > 3 and slope.posY > 1.5, "slope traversal failed")
  assert(math.abs(rider.posX-elapsed) < .7 and rider.posY > .7, "moving platform failed")
  assert(edge.posX > 3 and edge.posY < -1, "edge departure failed")
  assert(fast.posX > 4 and fast.posX < 5, "fast character tunneled")
  assert(ccd.posX > 3 and ccd.posX < 5, "CCD body tunneled")
  for i=0,3 do
   local p=pose("Stack"..i)
   assert(math.abs(p.posY-(.5+i)) < .25, "stack drift")
  end
  Log("physics-traversal-pass")
  SetShared("physicsPass",true)
  loaded = CallFunction("Scene.Load", {path="/Game/Scenes/Reload.21kbscene",additive=true})
  assert(loaded ~= nil and loaded ~= 0, "cooked scene reload failed")
  phase = 1
 elseif phase == 1 then
  if elapsed < 3.3 then return end
  ids.ReloadBody = World.FindByName("ReloadBody")
  local reloaded = pose("ReloadBody")
  assert(reloaded.found and reloaded.posY < 9.9, "reloaded body did not simulate")
  assert(CallFunction("Scene.Unload", {id=loaded}), "cooked scene unload failed")
  phase = 2
 elseif phase == 2 then
  local v=CallFunction("Physics.GetVelocity", {entity=ids.Stack0})
  assert(v.found, "scene unload removed original physics")
  assert(World.FindByName("ReloadBody") == 0, "scene unload left body alive")
  Log("physics-scene-lifecycle-pass")
  phase = 3
 end
end
"""
    (project / "Assets/Scripts/Probe.lua").write_text(source, encoding="utf-8")
    cli("scene-attach", "--scene", "/Game/Scenes/Main.21kbscene", "--node", "MainCamera", "--script", "/Game/Scripts/Probe.lua")
    output = cli("run", "--frames", "180", "--dt", ".02")
    assert "physics-traversal-pass" in output and "physics-scene-lifecycle-pass" in output and "0 diagnostics" in output, output
    package.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack", "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
    shutil.copy2(args.player, package / "Sample.exe")
    for dependency in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(dependency, package / dependency.name)
    unavailable = root / "SourceUnavailable"
    assert project.resolve().parent == root.resolve() and unavailable.resolve().parent == root.resolve()
    project.rename(unavailable)
    output = run(package / "Sample.exe", "--headless", "--frames=480", "--width=64", "--height=64", cwd=package)
    assert "physics-traversal-pass" in output and "physics-scene-lifecycle-pass" in output and "shutdown=clean" in output and "errors=0" in output, output
    (root / "result.json").write_text(json.dumps(dict(result="PASS",sourceUnavailable=True,stairs=True,slope=True,movingPlatform=True,edge=True,fastCharacter=True,ccd=True,stack=True,sceneLoadUnload=True),indent=2),encoding="utf-8")
    print(root)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

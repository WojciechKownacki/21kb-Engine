"""Verify the shipped Lua movement template from public authoring to a cooked game."""
from pathlib import Path
import json
import re
import shlex
import subprocess

from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import require


def author_fixture(args, root):
    def configure(run, component, project):
        workflow_project = project
        project = root / "Starter"
        run(args.cli, "new", "--parent", root, "--name", "Starter", "--template", "Empty")
        run(args.cli, "init-agent", "--project", project)
        guide = (project / "AGENTS.md").read_text(encoding="utf-8")
        require("entities and components (ECS)" in guide and "Start with Lua" in guide,
            "Fresh project must explain the recommended language and entity/component model")
        require("installed SDK and player build" in guide,
            "Fresh project must distinguish host verification from export platform support")
        catalog = json.loads((project / ".kb/api/script_api.json").read_text(encoding="utf-8"))
        functions = {entry["name"] for entry in catalog["functions"]}
        require({"Input.Vector2", "Transform.Translate", "Save.Read", "Save.Write"} <= functions,
            "Generated API must contain the starter's live input, movement and save functions")
        discovery = subprocess.run([str(args.cli), "commands"], capture_output=True, text=True,
            encoding="utf-8", check=True, timeout=30)
        commands = {entry["name"] for entry in json.loads(discovery.stdout)["commands"]}
        documented = set(re.findall(r"kb_cli ([a-z][a-z0-9-]*)", guide))
        require(documented <= commands, "Generated guide names unregistered commands: " + str(documented - commands))
        example = re.search(r"```sh\n(.*?)\n```", guide, re.DOTALL)
        require(example is not None, "Fresh project must contain an executable starter example")
        for line in example.group(1).splitlines():
            words = shlex.split(line)
            require(words[0] == "kb_cli", "Starter example must use the public CLI")
            result = subprocess.run([str(args.cli), *words[1:]], cwd=project,
                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=60)
            require(result.returncode == 0, "Generated starter failed: " + line + "\n" + result.stdout + result.stderr)
            if words[1] == "run":
                execution = json.loads(result.stdout)
                require(execution["succeeded"] and execution["completed_frames"] == 2,
                    "Generated starter must finish its real headless simulation")
        # Regeneration must refresh API files without replacing authored instructions or scripts.
        script = project / "Assets/Logic/PlayerController.lua"
        original = script.read_bytes()
        guide_bytes = (project / "AGENTS.md").read_bytes()
        api_before = (project / ".kb/api/script_api.json").read_bytes()
        run(args.cli, "init-agent", "--project", project)
        require(script.read_bytes() == original and (project / "AGENTS.md").read_bytes() == guide_bytes,
            "Regeneration must preserve the project's starter and instructions")
        require((project / ".kb/api/script_api.json").read_bytes() == api_before,
            "An unchanged project must regenerate the same API catalog")
        (root / "starter-proof.json").write_text(json.dumps(dict(result="PASS",
            commands=len(documented), apiFunctions=len(functions), exampleExecuted=True,
            regenerationPreserved=True), indent=2), encoding="utf-8")
        project = workflow_project
        run(args.cli, "init-agent", "--project", project)
        # This preset writes the normal Move action and keyboard bindings.
        run(args.cli, "entity", "add", "--project", project, "--create", "Top Down Player",
            "--name", "MappingPlayer", "--physics", "2d", "--position", "-1000,0,0")
        run(args.cli, "entity", "add", "--project", project, "--name", "ControllerWall", "--position", "-998,0,0")
        component("ControllerWall", "Collider2D", "width=1", "height=10", "sideView=false")
        run(args.cli, "entity", "add", "--project", project, "--name", "TemplatePlayer")
        run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
            "--node", "TemplatePlayer", "--script", "/Game/Logic/PlayerController.lua")
        run(args.cli, "entity", "add", "--project", project, "--name", "QueryWall", "--position", "0,0,3")
        component("QueryWall", "Collider", "boxSize.x=100", "boxSize.y=100", "boxSize.z=1")
        run(args.cli, "validate", "--project", project, "Assets/Logic/PlayerController.lua")

    return author_storage(args, root, script_name="LuaWorkflow",
        script_source=Path(__file__).with_name("fixtures") / "LuaWorkflow.lua", configure=configure)


class WorkflowScenario(StorageScenario):
    marker = "lua-workflow"


def run_cases(args, root, report, origin):
    profile = root / "profile"
    with BrowserSession(args, root, profile, report, origin, "play", scenario_type=WorkflowScenario) as case:
        initial = case.ready()
        require(not initial["loaded"] and abs(initial["x"]) < 0.001, "Fresh template must start at rest")
        case.wait("authored collider reaches physics queries", lambda s: s["hit"])
        case.blue_button("initial-ui")
        case.client.call("Input.dispatchKeyEvent", type="keyDown", key="d", code="KeyD",
            windowsVirtualKeyCode=68, nativeVirtualKeyCode=68)
        try:
            case.wait("trusted keyboard moves shipped template", lambda s: s["x"] > 0.3 and s["hit"])
            contact = case.wait("starter character reaches authored wall",
                lambda s: -998.95 < s["controllerX"] < -998.65)
            blocked = case.wait("continued input keeps character behind wall",
                lambda s: s["frames"] >= contact["frames"] + 40)
            require(-998.95 < blocked["controllerX"] < -998.65,
                "The shipped character controller crossed the authored solid wall: " + str(blocked))
        finally:
            case.client.call("Input.dispatchKeyEvent", type="keyUp", key="d", code="KeyD",
                windowsVirtualKeyCode=68, nativeVirtualKeyCode=68)
        retreat_start = len(case.states())
        case.client.call("Input.dispatchKeyEvent", type="keyDown", key="a", code="KeyA",
            windowsVirtualKeyCode=65, nativeVirtualKeyCode=65)
        try:
            case.wait("starter character can retreat after wall contact",
                lambda s: s["controllerX"] < blocked["controllerX"] - 0.3, retreat_start)
        finally:
            case.client.call("Input.dispatchKeyEvent", type="keyUp", key="a", code="KeyA",
                windowsVirtualKeyCode=65, nativeVirtualKeyCode=65)
        pixels = case.picture("movement-ui", [[60, 100]])
        require(pixels[0][0] > 180, "Template movement and collision query must update rendered UI")
        case.click_save()
        saved = case.wait("native UI saves gameplay state", lambda s: s["written"] and s["status"] == "Clean")
        case.graphics()
    with BrowserSession(args, root, profile, report, origin, "restart", scenario_type=WorkflowScenario) as case:
        restored = case.ready()
        require(restored["loaded"] and abs(restored["saved"] - saved["saved"]) < 0.00001,
            "Restarted packaged game must read the exact saved position")
        require(abs(restored["x"]) < 0.001, "Template state must restart independently from saved data")
        require(abs(restored["controllerX"] + 1000) < 0.001,
            "Controller spawn must restart independently from its persisted position")
        case.blue_button("restart-ui")
        case.graphics()
        report["observations"].append(dict(check="save survives browser process restart", state=restored))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-lua-workflow/v1", description=__doc__, prefix="GameLuaWorkflow"))

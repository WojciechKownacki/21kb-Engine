"""Verify the shipped Lua movement template from public authoring to a cooked game."""
from pathlib import Path

from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import require


def author_fixture(args, root):
    def configure(run, component, project):
        run(args.cli, "init-agent", "--project", project)
        # This preset writes the normal Move action and keyboard bindings.
        run(args.cli, "entity", "add", "--project", project, "--create", "Top Down Player",
            "--name", "MappingPlayer", "--physics", "2d", "--position", "-1000,0,0")
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
        finally:
            case.client.call("Input.dispatchKeyEvent", type="keyUp", key="d", code="KeyD",
                windowsVirtualKeyCode=68, nativeVirtualKeyCode=68)
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
        case.blue_button("restart-ui")
        case.graphics()
        report["observations"].append(dict(check="save survives browser process restart", state=restored))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-lua-workflow/v1", description=__doc__, prefix="GameLuaWorkflow"))

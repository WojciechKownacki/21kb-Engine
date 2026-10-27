"""Verify browser controller polling through cooked input actions and native UI.

Controller samples are synthetic browser API values, not physical device proof.
The production Emscripten bridge, action assets, Lua and GPU rendering are real.
"""
from pathlib import Path
import json
import urllib.request

from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import RuntimeClient, require


def author_fixture(args, root):
    def configure(run, component, project):
        # The normal preset authors the project's Move and Jump action assets.
        run(args.cli, "entity", "add", "--project", project,
            "--create", "Top Down Player", "--name", "Player", "--physics", "2d")

    return author_storage(args, root, script_name="GamepadFlow",
        script_source=Path(__file__).with_name("fixtures") / "GamepadFlow.lua", configure=configure)


SAMPLES = """
window.kbVirtualPads=[];
window.kbMakePad=(index,x,y,pressed)=>({
    id:'Standard test controller',index,connected:true,mapping:'standard',
    timestamp:performance.now(),axes:[x,y,0,0],
    buttons:Array.from({length:17},(_,i)=>({pressed:pressed.includes(i),value:pressed.includes(i)?1:0}))
});
navigator.getGamepads=()=>kbVirtualPads;
"""


class GamepadScenario(StorageScenario):
    marker = "gamepad-flow"

    def controls(self, label, x, y, jump, first, fourth, cursor=0):
        return self.wait(label, lambda s: s["ready"] and s["focus"] and
            abs(s["x"] - x) < 0.001 and abs(s["y"] - y) < 0.001 and
            s["jump"] == jump and s["first"] == first and s["fourth"] == fourth, cursor)

    def samples(self, label, expression, x, y, jump, first, fourth):
        cursor = len(self.states())
        self.client.evaluate(expression)
        return self.controls(label, x, y, jump, first, fourth, cursor)


def run_cases(args, root, report, origin):
    report["inputSource"] = "synthetic browser Gamepad samples; no physical controller claim"
    with BrowserSession(args, root, root / "profile", report, origin, "controllers", SAMPLES,
            scenario_type=GamepadScenario) as case:
        case.ready()
        case.controls("initial native controls", 0, 0, 0, False, False)
        case.blue_button("idle-native-ui")
        case.samples("standard browser pad reaches cooked actions",
            "kbVirtualPads=[kbMakePad(0,.25,-.5,[0]),null,null,kbMakePad(3,-1,1,[1])]",
            .25, .5, 1, True, True)
        pixels = case.picture("controller-updates-native-ui", [[60, 100]])
        require(pixels[0][0] > 150, "The controller action did not update the rendered native UI")
        case.samples("disconnect releases native actions without losing the fourth pad",
            "kbVirtualPads[0]=null", 0, 0, 0, False, True)
        case.blue_button("disconnected-native-ui")
        case.samples("reconnect restores cooked actions", "kbVirtualPads[0]=kbMakePad(0,-.5,.25,[0])",
            -.5, -.25, 1, True, True)
        case.samples("unmapped controller releases its actions", "kbVirtualPads[0].mapping=''",
            0, 0, 0, True, True)
        case.samples("standard mapping resumes", "kbVirtualPads[0].mapping='standard'",
            -.5, -.25, 1, True, True)
        client = case.client
        identity = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        other = None
        cursor = len(case.states())
        try:
            port = client.socket.sock.getpeername()[1]
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                target = next(item for item in json.load(response) if item["id"] == identity)
            other = RuntimeClient(target["webSocketDebuggerUrl"])
            other.call("Page.enable")
            other.call("Page.bringToFront")
            require(client.evaluate("!document.hasFocus() && document.visibilityState==='hidden'"),
                "The second tab did not cause actual focus loss")
            client.evaluate("kbVirtualPads=[]")
            client.call("Page.bringToFront")
        finally:
            client.call("Target.closeTarget", targetId=identity)
            if other is not None:
                other.close()
        case.controls("disconnect while unfocused leaves no stuck action", 0, 0, 0, False, False, cursor)
        case.samples("controller returns after focus loss", "kbVirtualPads=[kbMakePad(0,0,0,[12])]",
            0, 1, 0, True, False)
        case.samples("button release clears the D-pad action", "kbVirtualPads=[kbMakePad(0,0,0,[])]",
            0, 0, 0, True, False)
        case.graphics()


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-gamepad-tests/v1", description=__doc__, prefix="GameGamepad"))

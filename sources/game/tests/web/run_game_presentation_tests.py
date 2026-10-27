"""Verify browser presentation requests and native relative input in a cooked game."""
from pathlib import Path
import json
import time
import urllib.request

from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import RuntimeClient, require, wait_page_focus


def author_fixture(args, root):
    return author_storage(args, root, script_name="PresentationFlow",
        script_source=Path(__file__).with_name("fixtures") / "PresentationFlow.lua")


class PresentationScenario(StorageScenario):
    marker = "presentation-flow"

    def request(self, mode, enabled, gesture=False):
        expression = f"Module.kbPresentation.request({json.dumps(mode)},{str(enabled).lower()})"
        if gesture:
            # A real button click grants activation. Runtime.evaluate never
            # supplies its userGesture override to the tested request.
            self.client.evaluate("(() => {const b=document.createElement('button');"
                "b.id='presentation-test';b.textContent='Change display';"
                "b.style='position:fixed;left:10px;top:10px;z-index:100';"
                f"b.onclick=()=>{{window.kbPresentationResult=null;{expression}.then(v=>window.kbPresentationResult=v)}};"
                "document.body.append(b);})()")
            for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
                self.client.call("Input.dispatchMouseEvent", type=kind, x=30, y=20,
                    button="left", buttons=buttons, clickCount=1)
            deadline = time.monotonic() + 8
            while True:
                value = self.client.evaluate("window.kbPresentationResult")
                if value is not None:
                    break
                require(time.monotonic() < deadline, "Presentation request never completed")
                time.sleep(0.025)
            self.client.evaluate("document.querySelector('#presentation-test').remove()")
        else:
            value = self.client.call("Runtime.evaluate", expression=expression,
                awaitPromise=True, returnByValue=True)
            require("exceptionDetails" not in value, str(value))
            value = value["result"]["value"]
        self.report["observations"].append(dict(check=f"{mode} request {enabled}, gesture={gesture}", result=value))
        return value

    def inactive(self, mode):
        deadline = time.monotonic() + 8
        while True:
            value = self.client.evaluate(f"Module.kbPresentation.state().{mode}")
            if not value["active"] and not value["pending"]:
                self.report["observations"].append(dict(check=mode + " released", state=value))
                return
            require(time.monotonic() < deadline, "Presentation stayed captured: " + str(value))
            time.sleep(0.025)


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "presentation",
            scenario_type=PresentationScenario) as case:
        case.ready()
        client = case.client
        case.blue_button("initial-native-ui")
        capabilities = client.evaluate("Module.kbPresentation.state()")
        require(all(value["supported"] for value in capabilities.values()),
            "This desktop browser must support both presentation modes: " + str(capabilities))
        report["observations"].append(dict(check="actual browser capabilities", state=capabilities))
        for mode in ["pointerLock", "fullscreen"]:
            result = case.request(mode, True)
            require(not result["ok"] and result["error"] == "user-activation-required", str(result))
            case.inactive(mode)

        require(case.request("pointerLock", True, True)["ok"], "Trusted pointer lock failed")
        before = case.states()[-1]
        # CDP supplies trusted motion to the locked canvas. Compare native
        # Lua totals with the actual DOM movement, including multiple events.
        client.evaluate("window.kbMotion=[0,0];document.querySelector('#canvas').addEventListener('mousemove',"
            "e=>{if(document.pointerLockElement){kbMotion[0]+=e.movementX;kbMotion[1]+=e.movementY;}})")
        for x, y in [(40, 35), (70, 60), (100, 80)]:
            client.call("Input.dispatchMouseEvent", type="mouseMoved", x=x, y=y, button="none", buttons=0)
        case.paint()
        movement = client.evaluate("kbMotion")
        require(abs(movement[0]) + abs(movement[1]) > 0, "Browser did not deliver relative mouse movement")
        case.wait("relative movement reaches native Pointer.Delta", lambda s:
            abs(s["totalX"] - before["totalX"] - movement[0]) < 0.01 and
            abs(s["totalY"] - before["totalY"] - movement[1]) < 0.01)
        require(case.request("pointerLock", False)["ok"], "Explicit pointer release failed")
        case.inactive("pointerLock")
        before = case.states()[-1]
        client.call("Input.dispatchMouseEvent", type="mouseMoved", x=500, y=400, button="none", buttons=0)
        case.paint()
        after = case.states()[-1]
        require(abs(after["totalX"] - before["totalX"]) < 0.01 and
            abs(after["totalY"] - before["totalY"]) < 0.01, "Unlock introduced an absolute-motion jump")
        report["observations"].append(dict(check="unlock restores absolute position without camera jump", state=after))

        for mode in ["pointerLock", "fullscreen"]:
            require(case.request(mode, True, True)["ok"], "Trusted request failed: " + mode)
            client.call("Input.dispatchMouseEvent", type="mousePressed", x=100, y=100,
                button="left", buttons=1, clickCount=1)
            case.wait("native button held before Escape", lambda s: s["pressed"], len(case.states()))
            cursor = len(case.states())
            for kind in ["keyDown", "keyUp"]:
                client.call("Input.dispatchKeyEvent", type=kind, key="Escape", code="Escape", windowsVirtualKeyCode=27)
            case.inactive(mode)
            if mode == "pointerLock":
                case.wait("unlock clears held native button without mouseup", lambda s: not s["pressed"], cursor)
            client.call("Input.dispatchMouseEvent", type="mouseReleased", x=100, y=100,
                button="left", buttons=0, clickCount=1)
            case.blue_button(mode + "-escape-native-ui")

        require(case.request("fullscreen", True, True)["ok"], "Fullscreen retry failed")
        case.geometry()
        case.blue_button("fullscreen-native-ui")
        require(case.request("fullscreen", False)["ok"], "Explicit fullscreen exit failed")
        case.inactive("fullscreen")

        for mode in ["pointerLock", "fullscreen"]:
            require(case.request(mode, True, True)["ok"], "Focus test entry failed: " + mode)
            identity = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
            other = None
            try:
                port = client.socket.sock.getpeername()[1]
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                    target = next(item for item in json.load(response) if item["id"] == identity)
                other = RuntimeClient(target["webSocketDebuggerUrl"])
                other.call("Page.enable")
                other.call("Page.bringToFront")
                wait_page_focus(client, False)
                case.inactive(mode)
                result = case.request(mode, True)
                require(not result["ok"] and result["error"] == "unfocused", str(result))
                client.call("Page.bringToFront")
                wait_page_focus(client, True)
            finally:
                client.call("Target.closeTarget", targetId=identity)
                if other is not None:
                    other.close()
            case.inactive(mode)
            require(case.request(mode, True, True)["ok"], "Gesture retry after focus loss failed")
            require(case.request(mode, False)["ok"], "Release after recovery failed")
            case.inactive(mode)

        # The browser itself denies pointer capture in a sandbox without
        # allow-pointer-lock. Run the real exported player in that frame.
        client.evaluate("(() => {const f=document.createElement('iframe');f.id='denied-player';"
            "f.sandbox='allow-scripts allow-same-origin';f.src='Game.html';"
            "f.style='position:fixed;left:300px;top:300px;width:400px;height:300px;border:0';"
            "document.body.append(f);})()")
        deadline = time.monotonic() + 45
        while not client.evaluate("document.querySelector('#denied-player').contentDocument?.documentElement.dataset.kbFirstFrame==='true'"):
            require(time.monotonic() < deadline, "Sandboxed packaged player did not start")
            time.sleep(0.05)
        client.evaluate("document.querySelector('#denied-player').contentWindow.eval(\""
            "const b=document.createElement('button');b.textContent='Capture';"
            "b.style='position:fixed;left:10px;top:10px';b.onclick=()=>Module.kbPresentation.request('pointerLock',true).then(v=>window.deniedResult=v);"
            "document.body.append(b)\")")
        for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
            client.call("Input.dispatchMouseEvent", type=kind, x=330, y=320,
                button="left", buttons=buttons, clickCount=1)
        deadline = time.monotonic() + 8
        while True:
            denied = client.evaluate("document.querySelector('#denied-player').contentWindow.deniedResult")
            if denied is not None:
                break
            require(time.monotonic() < deadline, "Sandbox browser denial never completed")
            time.sleep(0.025)
        require(not denied["ok"] and denied["error"] in ["denied", "SecurityError"] and not denied["active"],
            "Expected actual browser permission denial: " + str(denied))
        report["observations"].append(dict(check="actual sandbox browser denial", result=denied))
        client.evaluate("document.querySelector('#denied-player').remove();window.focus()")
        case.blue_button("recovered-native-ui")
        case.graphics()


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-presentation-tests/v1", description=__doc__, prefix="GamePresentation"))

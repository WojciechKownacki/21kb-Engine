"""Verify cooked Lua haptics calls through the Web host and browser actuator API.

Actuators are synthetic API spies; this does not measure physical vibration.
"""
from pathlib import Path
import json
import urllib.request

from run_game_gamepad_tests import SAMPLES
from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import RuntimeClient, require


def author_fixture(args, root):
    def configure(run, component, project):
        run(args.cli, "entity", "add", "--project", project,
            "--create", "Top Down Player", "--name", "Player", "--physics", "2d")
    return author_storage(args, root, script_name="HapticFlow",
        script_source=Path(__file__).with_name("fixtures") / "HapticFlow.lua", configure=configure)


ACTUATORS = SAMPLES + """
window.kbEffects=[];
window.kbReject=false;
window.kbAttach=(index)=>{
    const pad=kbMakePad(index,0,0,[]);
    pad.vibrationActuator={effects:['dual-rumble'],
        playEffect:(kind,values)=>{
            kbEffects.push({index,kind,...values});
            return kbReject ? Promise.reject(new Error('actuator rejected')) : Promise.resolve('complete');
        },
        reset:()=>{kbEffects.push({index,kind:'reset'});return Promise.resolve('complete');}
    };
    kbVirtualPads[index]=pad;
};
"""


class HapticScenario(StorageScenario):
    marker = "haptic-flow"

    def command(self, label, x, applied=True):
        initial = self.states()[-1]["sequence"]
        self.client.evaluate(f"kbVirtualPads[0].axes[0]={x};kbVirtualPads[0].buttons[0]={{pressed:true,value:1}}")
        value = self.wait(label, lambda s: s["sequence"] == initial + 1 and s["applied"] == applied)
        self.client.evaluate("kbVirtualPads[0].buttons[0]={pressed:false,value:0}")
        self.paint()
        return value


def run_cases(args, root, report, origin):
    report["actuatorSource"] = "synthetic browser API; production cooked Lua, scene facade and Web host"
    with BrowserSession(args, root, root / "profile", report, origin, "haptics", ACTUATORS,
            scenario_type=HapticScenario) as case:
        initial = case.ready()
        require(not initial["connected"] and not initial["supported"], "Absent controller reported haptics")
        case.client.evaluate("kbVirtualPads=[kbMakePad(0,0,0,[])]")
        case.wait("connected controller without actuator", lambda s: s["connected"] and not s["supported"])
        case.command("unsupported controller refuses effect", 0, False)
        case.client.evaluate("kbAttach(0);kbAttach(3)")
        case.wait("both providers available", lambda s: s["supported"] and s["fourth"])
        case.command("Lua starts first controller", 0)
        require(case.client.evaluate("kbEffects.some(e=>e.index===0&&e.kind==='dual-rumble'&&Math.abs(e.strongMagnitude-.6)<.001&&Math.abs(e.weakMagnitude-.3)<.001&&e.duration===250)"), "Motor request did not reach browser")
        for _ in range(3):
            case.wait("active effect continues through host ticks", lambda s: s["supported"], len(case.states()))
        require(case.client.evaluate("kbEffects.filter(e=>e.index===0&&e.kind==='dual-rumble').length>1"),
            "Host did not refresh the bounded effect")
        case.command("local user routes fourth controller", 1)
        require(case.client.evaluate("kbEffects.some(e=>e.index===3&&Math.abs(e.strongMagnitude-.2)<.001&&Math.abs(e.weakMagnitude-.7)<.001)"), "User routing missed fourth actuator")
        case.command("Lua stops active controllers", -1)
        require(case.client.evaluate("[0,3].every(i=>kbEffects.some(e=>e.index===i&&e.kind==='reset'))"), "Stop did not reset both actuators")
        case.client.evaluate("kbEffects=[];kbReject=true")
        case.command("request accepted before asynchronous rejection", 0)
        case.wait("asynchronous rejection removes capability", lambda s: s["connected"] and not s["supported"])
        case.command("rejected actuator refuses subsequent request", 0, False)
        case.client.evaluate("kbReject=false;kbAttach(0)")
        case.wait("replacement actuator restores capability", lambda s: s["supported"])
        case.command("replacement actuator starts", 0)
        client = case.client
        client.evaluate("kbEffects=[]")
        identity = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        other = None
        try:
            port = client.socket.sock.getpeername()[1]
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                target = next(item for item in json.load(response) if item["id"] == identity)
            other = RuntimeClient(target["webSocketDebuggerUrl"])
            other.call("Page.enable")
            other.call("Page.bringToFront")
            require(client.evaluate("!document.hasFocus() && document.visibilityState==='hidden'"),
                "Second tab did not hide the game")
            require(client.evaluate("kbEffects.some(e=>e.index===0&&e.kind==='reset')"),
                "Actual focus loss did not stop the actuator")
            client.evaluate("kbEffects=[]")
            client.call("Page.bringToFront")
        finally:
            client.call("Target.closeTarget", targetId=identity)
            if other is not None:
                other.close()
        cursor = len(case.states())
        case.wait("focus returns with haptics capability", lambda s: s["supported"], cursor)
        require(client.evaluate("kbEffects.length===0"), "Focus restoration resumed the old effect")
        case.command("explicit command after focus restoration", 0)
        case.client.evaluate("kbEffects=[];kbVirtualPads[0]=null")
        case.wait("disconnect clears capability", lambda s: not s["connected"] and not s["supported"])
        require(case.client.evaluate("kbEffects.some(e=>e.index===0&&e.kind==='reset')"), "Disconnect did not stop active actuator")
        case.blue_button("native-ui-after-haptics")
        case.graphics()


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-haptics-tests/v1", description=__doc__, prefix="GameHaptics"))

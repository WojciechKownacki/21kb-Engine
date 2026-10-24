"""Verify independent native save slots in simultaneous pages of one export."""
from __future__ import annotations

import json
from pathlib import Path
import urllib.request

from run_game_storage_tests import BrowserSession, StorageScenario, main
from run_game_text_tests import author_fixture as author_text_fixture
from run_game_touch_tests import OBSERVER, RuntimeClient, require


INPUT_OBSERVER = """
window.kbStoragePageInputs=[];
for(const kind of ['input','keydown','keyup','mousedown','mouseup']) {
    document.addEventListener(kind, event => kbStoragePageInputs.push({
        type:event.type, trusted:event.isTrusted
    }), true);
}
"""


def author_fixture(args, root):
    return author_text_fixture(args, root, script_name="StorageSlots",
        script_source=Path(__file__).with_name("fixtures") / "StorageSlots.lua")


class PageScenario(StorageScenario):
    def submit(self, slot):
        self.geometry()
        for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
            self.client.call("Input.dispatchMouseEvent", type=kind,
                x=self.layout["left"] + 140, y=self.layout["top"] + 350,
                button="left", buttons=buttons, clickCount=1)
            self.paint()
        self.client.call("Input.insertText", text=slot)
        self.paint()
        for kind in ["keyDown", "keyUp"]:
            self.client.call("Input.dispatchKeyEvent", type=kind, key="Enter", code="Enter",
                windowsVirtualKeyCode=13, nativeVirtualKeyCode=13)
            self.paint()
        self.wait("native slot " + slot + " committed", lambda value:
            value["slot"] == slot and value["written"] and value["started"] and value["status"] == "Clean")
        events = self.client.evaluate("kbStoragePageInputs")
        require(events and all(event["trusted"] for event in events) and
            all(any(event["type"] == kind for event in events)
                for kind in ["input", "keydown", "keyup", "mousedown", "mouseup"]),
            "The slot selection did not use trusted native input: " + str(events))

    def field_picture(self, label):
        pixels = self.picture(label, [[60, 380]])
        require(pixels[0][2] > pixels[0][0] + 50,
            "The native slot field did not render: " + str(pixels))


def run_cases(args, root, report, origin):
    secondary = []
    with BrowserSession(args, root, root / "profile", report, origin, "primary", INPUT_OBSERVER) as main_page:
        first = PageScenario(main_page.client, root, report)
        initial = first.ready()
        require(not initial["first"] and not initial["second"], "The fresh profile already has slots")

        def page(label):
            identity = first.client.call("Target.createTarget", url="about:blank")["targetId"]
            port = first.client.socket.sock.getpeername()[1]
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                info = next(value for value in json.load(response) if value["id"] == identity)
            client = RuntimeClient(info["webSocketDebuggerUrl"])
            secondary.append((label, identity, client))
            client.call("Runtime.enable")
            client.call("Page.enable")
            client.call("Page.bringToFront")
            client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
                deviceScaleFactor=1, mobile=False)
            client.call("Page.addScriptToEvaluateOnNewDocument", source=OBSERVER + INPUT_OBSERVER)
            client.call("Page.navigate", url=origin + "/Game.html")
            scenario = PageScenario(client, root, report)
            return scenario, scenario.ready()

        try:
            second, second_initial = page("second")
            require(not second_initial["first"] and not second_initial["second"],
                "Both working filesystems must start before either write")
            first.client.call("Page.bringToFront")
            first.paint()
            first.submit("slot_1")
            first.field_picture("first-slot-field")
            second.client.call("Page.bringToFront")
            second.paint()
            second.submit("slot_2")
            second.field_picture("second-slot-field")
            third, value = page("fresh-reader")
            report["freshReader"] = value
            report["graphics"] = third.graphics()
            require(value["first"] and value["second"] and value["firstValue"] == "value-slot_1" and
                value["secondValue"] == "value-slot_2",
                "An independent page flush deleted or replaced another named slot: " + str(value))
            report["observations"].append(dict(check="both independent native slots survive fresh page load", state=value))
        finally:
            for label, identity, client in secondary:
                try:
                    state = PageScenario(client, root, report).states()
                    (root / (label + "-states.json")).write_text(json.dumps(state, indent=2), encoding="utf-8")
                except Exception:
                    pass
                (root / (label + "-events.json")).write_text(json.dumps(client.events, indent=2), encoding="utf-8")
                try:
                    first.client.call("Target.closeTarget", targetId=identity)
                except Exception:
                    pass
                client.close()


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-storage-pages/v1", description=__doc__, prefix="StoragePages"))

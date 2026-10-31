"""Cook and verify native text editing through trusted browser input and IME."""
from __future__ import annotations

import json
from pathlib import Path
import shutil
import subprocess

from run_game_touch_tests import TouchScenario, main, read_png, require


def author_fixture(args, root, *, script_name="TextFlow", script_source=None):
    project = root / "Sample"
    web = root / "Web"
    web.mkdir()
    commands = []

    def run(executable, *tail):
        command = list(map(str, [executable, *tail]))
        result = subprocess.run(command, cwd=root, capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=180,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        commands.append(dict(command=command, exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(commands, indent=2), encoding="utf-8")
        require(result.returncode == 0, "Authoring/cook failed: " + str(commands[-1]))

    def component(entity, kind, *settings):
        tail = ["component", "add", "--project", project, "--entity", entity, "--type", kind]
        for setting in settings:
            tail += ["--set", setting]
        run(args.cli, *tail)

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    fonts = args.engine_root / "sources/editor/content/EditorShell/Fonts"
    run(args.cli, "import", "--project", project, "--destination", "/Game/Fonts", fonts / "DejaVuSans.ttf")
    shutil.copy2(fonts / "DejaVu-LICENSE.txt", web / "DejaVu-LICENSE.txt")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "CanvasRoot")
    component("CanvasRoot", "Rect Transform", "anchorMax=1,1", "offsetMax=0,0")
    component("CanvasRoot", "Canvas")
    component("CanvasRoot", "Canvas Scaler", "scaleMode=0", "scaleFactor=1")
    run(args.cli, "entity", "add", "--project", project, "--name", "Form", "--parent", "CanvasRoot")
    component("CanvasRoot/Form", "Rect Transform", "anchorMax=1,1", "offsetMax=0,0")
    component("CanvasRoot/Form", "Mask")
    component("CanvasRoot/Form", "Scroll View", "horizontal=false", "inertia=false")
    run(args.cli, "entity", "add", "--project", project, "--name", "TextEntry", "--parent", "CanvasRoot/Form")
    component("CanvasRoot/Form/TextEntry", "Rect Transform", "offsetMin=40,320", "offsetMax=560,400")
    component("CanvasRoot/Form/TextEntry", "Border", "backgroundColor.r=0.05",
        "backgroundColor.g=0.1", "backgroundColor.b=0.5", "backgroundColor.a=1")
    component("CanvasRoot/Form/TextEntry", "Text", "fontAssetId=/Game/Fonts/DejaVuSans.21kb")
    component("CanvasRoot/Form/TextEntry", "Selectable", "colorFadeSeconds=0")
    component("CanvasRoot/Form/TextEntry", "Input Field")
    run(args.cli, "entity", "add", "--project", project, "--name", "OtherEntry", "--parent", "CanvasRoot/Form")
    component("CanvasRoot/Form/OtherEntry", "Rect Transform", "offsetMin=40,100", "offsetMax=560,180")
    component("CanvasRoot/Form/OtherEntry", "Text", "fontAssetId=/Game/Fonts/DejaVuSans.21kb")
    component("CanvasRoot/Form/OtherEntry", "Selectable", "colorFadeSeconds=0")
    component("CanvasRoot/Form/OtherEntry", "Input Field", "multiline=true", "characterLimit=8")
    run(args.cli, "script-new", "--project", project, "--name", script_name, "--language", "lua")
    shutil.copy2(script_source or Path(__file__).with_name("fixtures") / "TextFlow.lua",
        project / ("Assets/Scripts/" + script_name + ".lua"))
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "TextEntry", "--script", "/Game/Scripts/" + script_name + ".lua")
    run(args.cooker, "--project", project, "--target", "WebGL.wasm32",
        "--output", web / "Game.kbpack", "--engine-root", args.engine_root,
        "--shaderc", args.shaderc, "--cache", root / "CookCache")
    for name in ["Game.html", "Game.js", "Game.wasm"]:
        shutil.copy2(args.directory / name, web / name)
    return web


class TextScenario(TouchScenario):
    marker = "text-flow"

    def click(self, x, y):
        layout = self.geometry()
        for kind, buttons in [("mousePressed", 1), ("mouseReleased", 0)]:
            self.client.call("Input.dispatchMouseEvent", type=kind,
                x=layout["left"] + x / layout["dpr"],
                y=layout["top"] + y / layout["dpr"],
                button="left", buttons=buttons, clickCount=1)

    def key(self, key, code, virtual, modifiers=0):
        if modifiers & 2:
            self.client.call("Input.dispatchKeyEvent", type="keyDown", key="Control", code="ControlLeft",
                windowsVirtualKeyCode=17, nativeVirtualKeyCode=17, modifiers=2)
            self.paint()
        for kind in ["keyDown", "keyUp"]:
            self.client.call("Input.dispatchKeyEvent", type=kind, key=key, code=code,
                windowsVirtualKeyCode=virtual, nativeVirtualKeyCode=virtual, modifiers=modifiers)
            self.paint()
        if modifiers & 2:
            self.client.call("Input.dispatchKeyEvent", type="keyUp", key="Control", code="ControlLeft",
                windowsVirtualKeyCode=17, nativeVirtualKeyCode=17)
            self.paint()

    def field_picture(self, label):
        pixels = self.picture(label, [[60, 380]])
        require(pixels[0][2] > pixels[0][0] + 50,
            "The native input field did not render: " + str(pixels))
        if label == "edited":
            rows = read_png(self.root / (label + ".png"))
            # Small antialiased glyphs need contrast, not fully white pixels.
            # Requiring multiple columns also rejects a caret without text.
            bright = [(x, y) for y in range(320, 400) for x in range(40, 560)
                if all(rows[y][x][channel] > pixels[0][channel] + 64 for channel in [0, 1])]
            require(len(bright) > 50 and len({x for x, _ in bright}) > 8,
                "The actual text glyphs did not render; a caret alone is insufficient")
            self.report["renderedText"] = dict(brightPixels=len(bright), columns=len({x for x, _ in bright}))

    def run(self, targets):
        client = self.client
        self.wait("native field ready after eight game ticks", lambda value: value["entry"] != 0 and value["ready"])
        client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{width:'800px',height:'600px',margin:'0',display:'block'})")
        self.paint()
        self.field_picture("initial")
        client.evaluate("window.kbTextEvents=[]; for(const kind of "
            "['beforeinput','input','compositionstart','compositionupdate','compositionend']) "
            "document.addEventListener(kind,e=>kbTextEvents.push({type:e.type,data:e.data,"
            "trusted:e.isTrusted,composing:e.isComposing,inputType:e.inputType}),true)")
        self.click(140, 350)
        self.wait("hit test focuses native field", lambda value: value["focused"] == value["entry"])
        dom = client.evaluate("({tag:document.activeElement.tagName,"
            "label:document.activeElement.getAttribute('aria-label')})")
        require(dom == dict(tag="TEXTAREA", label="Game text input"),
            "Native field did not activate the normal browser text control: " + str(dom))
        self.report["domFocus"] = dom
        text = "Test żółw 😀"
        client.call("Input.insertText", text=text)
        self.wait("committed UTF8 reaches native field", lambda value: value["text"] == text)
        client.call("Input.imeSetComposition", text="仮", selectionStart=1, selectionEnd=1)
        self.paint()
        require(self.states()[-1]["text"] == text, "Uncommitted IME text reached the engine")
        self.report["uncommitted"] = self.states()[-1]
        self.key("ArrowLeft", "ArrowLeft", 37)
        require(self.states()[-1]["text"] == text, "IME candidate navigation changed committed text")
        client.call("Input.insertText", text="確定")
        text += "確定"
        self.wait("IME commit reaches native field once", lambda value: value["text"] == text)
        client.call("Input.insertText", text="確定")
        text += "確定"
        self.wait("identical independent text after IME is preserved", lambda value: value["text"] == text)
        self.key("Backspace", "Backspace", 8)
        text = text[:-1]
        self.wait("Backspace removes one UTF8 character", lambda value: value["text"] == text)
        client.call("Input.insertText", text="😀")
        self.wait("supplementary character inserted", lambda value: value["text"] == text + "😀")
        self.key("Backspace", "Backspace", 8)
        self.wait("Backspace removes a full supplementary character", lambda value: value["text"] == text)
        self.key("ArrowLeft", "ArrowLeft", 37)
        self.key("Delete", "Delete", 46)
        text = text[:-1]
        self.wait("Delete removes one full UTF8 character at the caret", lambda value: value["text"] == text)
        self.key("Enter", "Enter", 13)
        self.wait("Enter submits once without inserting a newline",
            lambda value: value["submitted"] == 1 and value["text"] == text)
        self.key("Escape", "Escape", 27)
        self.wait("Escape cancels once without changing committed text",
            lambda value: value["canceled"] == 1 and value["submitted"] == 1 and value["text"] == text)
        self.field_picture("edited")
        cursor = len(self.states())
        other = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        try:
            client.call("Target.activateTarget", targetId=other)
            require(client.evaluate("!document.hasFocus() && document.visibilityState==='hidden'"),
                "The owned tab did not cause actual focus loss")
            client.call("Page.bringToFront")
        finally:
            client.call("Target.closeTarget", targetId=other)
        self.wait("focus return preserves text without extra submission",
            lambda value: value["hasFocus"] and value["submitted"] == 1 and value["text"] == text, cursor)
        client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
            deviceScaleFactor=2, mobile=False)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,{width:'960px',height:'540px'})")
        self.paint()
        self.geometry()
        self.click(140, 350)
        self.wait("field refocused after DPI resize", lambda value: value["focused"] == value["entry"])
        client.call("Input.insertText", text="!")
        self.wait("editing continues after DPI resize", lambda value: value["text"] == text + "!")
        self.field_picture("resized")
        client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
            deviceScaleFactor=1, mobile=False)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,{width:'800px',height:'280px'})")
        self.paint()
        self.geometry()
        self.wait("smaller viewport reveals the focused text field",
            lambda value: value["focused"] == value["entry"] and value["scrollY"] >= 119.9)
        pixels = self.picture("smaller-viewport", [[60, 260]])
        require(pixels[0][2] > pixels[0][0] + 50,
            "The focused field is outside the actual smaller viewport: " + str(pixels))
        self.click(140, 230)
        self.key("End", "End", 35)
        client.call("Input.insertText", text="?")
        self.wait("revealed field continues editing", lambda value: value["text"] == text + "!?")
        events = client.evaluate("window.kbTextEvents")
        self.report["textEvents"] = events
        commits = [event for event in events if event["type"] == "compositionend"]
        require(len(commits) == 1 and commits[0]["data"] == "確定", "The IME commit event is missing or duplicated")
        # DevTools insertText can end composition with isTrusted=false. Retain
        # that qualification; every other observed text event must be trusted.
        require(events and all(event["trusted"] for event in events if event["type"] != "compositionend"),
            "Text input or composition start/update events were not trusted")
        self.report["compositionEndTrusted"] = commits[0]["trusted"]
        require(all(any(event["type"] == kind for event in events)
            for kind in ["input", "beforeinput", "compositionstart", "compositionupdate", "compositionend"]),
            "The actual browser composition event path was not exercised")
        self.report.update(graphics=self.graphics(), finalLayout=dict(self.layout))
        self.key("a", "KeyA", 65, modifiers=2)
        self.key("Backspace", "Backspace", 8)
        self.wait("select all clears the native field", lambda value: value["text"] == "")
        long_commit = "x" * 63 + "e\u0301" + "👩‍👩‍👧‍👦"
        client.call("Input.imeSetComposition", text=long_commit,
            selectionStart=len(long_commit.encode("utf-16-le")) // 2,
            selectionEnd=len(long_commit.encode("utf-16-le")) // 2)
        self.paint()
        require(self.states()[-1]["text"] == "", "Long provisional composition reached the field")
        client.call("Input.insertText", text=long_commit)
        self.wait("long composition commits without splitting a grapheme",
            lambda value: value["text"] == long_commit)
        self.key("Backspace", "Backspace", 8)
        self.wait("emoji sequence deletes as one grapheme", lambda value: value["text"] == "x" * 63 + "e\u0301")
        self.key("Backspace", "Backspace", 8)
        self.wait("combining mark deletes with its base", lambda value: value["text"] == "x" * 63)
        self.key("a", "KeyA", 65, modifiers=2)
        copied = client.evaluate("""(() => {
            const data = new DataTransfer();
            document.activeElement.dispatchEvent(new ClipboardEvent('copy',
                {clipboardData:data,bubbles:true,cancelable:true}));
            return data.getData('text/plain');
        })()""")
        require(copied == "x" * 63, "Browser copy did not read the native selection")
        client.evaluate("document.activeElement.dispatchEvent(new ClipboardEvent('cut',"
            "{clipboardData:new DataTransfer(),bubbles:true,cancelable:true}))")
        self.wait("browser cut removes the native selection", lambda value: value["text"] == "")
        client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
            deviceScaleFactor=1, mobile=False)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,{width:'800px',height:'600px'})")
        self.paint()
        self.geometry()
        client.call("Input.imeSetComposition", text="未確定", selectionStart=3, selectionEnd=3)
        client.evaluate("window.retiredTextControl=document.activeElement")
        self.click(140, 140)
        self.wait("another native field receives focus", lambda value: value["focused"] == value["other"])
        require(self.states()[-1]["otherText"] == "", "Composition leaked into the next field")
        client.evaluate("retiredTextControl.dispatchEvent(new CompositionEvent('compositionend',{data:'late'}));"
            "retiredTextControl.dispatchEvent(new InputEvent('input',{inputType:'insertText',data:'stale'}))")
        self.paint()
        require(self.states()[-1]["otherText"] == "", "Retired text control accepted a delayed event")
        pasted = "e\u0301\n😀abcdefghi"
        client.evaluate("(() => {const data=new DataTransfer();data.setData('text/plain'," + json.dumps(pasted) + ");"
            "document.activeElement.dispatchEvent(new ClipboardEvent('paste',{clipboardData:data,bubbles:true,cancelable:true}));})()")
        self.wait("multiline paste respects the native grapheme limit",
            lambda value: value["otherText"] == "e\u0301\n😀abcde")
        self.report["clipboardTransport"] = "Isolated DataTransfer events through the full cooked engine; system clipboard untouched"


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario_type=TextScenario,
        schema="21kb.web-game-text-tests/v1", prefix="GameText", description=__doc__))

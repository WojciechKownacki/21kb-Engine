"""Verify public WASM loading options with native readiness and hardware GPU pixels."""
import os
import re

from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_delete_tests import author_fixture, verify
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


def configure_html(html, script, *, replace=False):
    pattern = r'''<script\b[^>]*\bsrc\s*=\s*(?:"Game\.js"|'Game\.js'|Game\.js(?=[\s>]))[^>]*>\s*</script>'''
    html, count = re.subn(pattern, lambda match: "<script>" + script + "</script>" +
        ("" if replace else match.group(0)), html)
    require(count == 1, "The exported startup script was not found")
    return html


def run_cases(args, root, report, origin):
    web = root / "Web"
    engine = web / "Engine"
    engine.mkdir()
    os.link(web / "Game.wasm", engine / "Runtime.wasm")
    configurations = {
        "LocateFile": ("""
            window.kbWasmOption = {calls:[]};
            Module.locateFile = function(name, prefix) {
                kbWasmOption.calls.push({name, prefix});
                return new URL('../Engine/Runtime.wasm', location.href).href;
            };
        """, False),
        "WasmBinary": ("""
            window.kbWasmOption = {calls:0};
            fetch('../Engine/Runtime.wasm').then(response => {
                if (!response.ok) throw new Error('Preloaded WASM request failed');
                return response.arrayBuffer();
            }).then(bytes => {
                Module.wasmBinary = new Uint8Array(bytes);
                kbWasmOption.bytes = bytes.byteLength;
                ++kbWasmOption.calls;
                const script = document.createElement('script');
                script.src = 'Game.js';
                document.body.appendChild(script);
            });
        """, True),
        "InstantiateWasm": ("""
            window.kbWasmOption = {calls:0};
            Module.instantiateWasm = function(imports, receiveInstance) {
                ++kbWasmOption.calls;
                WebAssembly.instantiateStreaming(fetch('../Engine/Runtime.wasm'), imports).then(result => {
                    receiveInstance(result.instance, result.module);
                });
                return {};
            };
        """, False)
    }
    html = (web / "Game.html").read_text(encoding="utf-8")
    for label, (configuration, replace) in configurations.items():
        folder = web / label
        folder.mkdir()
        (folder / "Game.html").write_text(configure_html(html, configuration, replace=replace), encoding="utf-8")
        for name in ["Game.js", "Game.kbpack"]:
            os.link(web / name, folder / name)
        # No default Game.wasm exists here: ignoring the option must fail startup.
        with BrowserSession(args, root, root / "profile", report, origin, label, LOAD_OBSERVER,
                route="/" + label) as case:
            verify(case.ready(), 0)
            case.blue_button("public-wasm-option-" + label.lower())
            option = case.client.evaluate("window.kbWasmOption")
            if label == "LocateFile":
                require(len(option["calls"]) == 1 and option["calls"][0]["name"] == "Game.wasm" and
                    option["calls"][0]["prefix"] == origin + "/" + label + "/",
                    "locateFile did not receive the script directory: " + str(option))
            else:
                require(option["calls"] == 1, "The custom module option ran more than once: " + str(option))
                if label == "WasmBinary":
                    require(option["bytes"] == (web / "Game.wasm").stat().st_size,
                        "The preloaded binary did not contain the actual built module")
            requests = case.client.evaluate("performance.getEntriesByType('resource').map(value=>({"
                "path:new URL(value.name).pathname,decodedBytes:value.decodedBodySize}))")
            modules = [value for value in requests if value["path"].endswith(".wasm")]
            require(len(modules) == 1 and modules[0]["path"] == "/Engine/Runtime.wasm" and
                modules[0]["decodedBytes"] == (web / "Game.wasm").stat().st_size,
                "Startup ignored the custom module or downloaded a second copy: " + str(modules))
            report["observations"].append(dict(check="public WASM option reaches native hardware rendering",
                variant=label, option=option, moduleRequests=modules))


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-wasm-options/v1", description=__doc__, prefix="WasmOptions"))

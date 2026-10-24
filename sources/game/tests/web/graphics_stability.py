"""Sustained checks of the actual packaged player's graphics and resource lifetime."""
from __future__ import annotations

import json
import re
import time


OBSERVER = """
window.kbStabilityLosses = [];
document.addEventListener('webglcontextlost', e => kbStabilityLosses.push(e.statusMessage), true);
window.kbGLResources = {};
for (const kind of ['Buffer','Framebuffer','Program','Renderbuffer','Shader','Texture','VertexArray']) {
    const live = new Set();
    const counts = {created:0,deleted:0,live:0,peak:0};
    kbGLResources[kind] = counts;
    const create = WebGL2RenderingContext.prototype['create' + kind];
    const destroy = WebGL2RenderingContext.prototype['delete' + kind];
    WebGL2RenderingContext.prototype['create' + kind] = function(...args) {
        const value = create.apply(this, args);
        if (value !== null) {
            live.add(value); counts.created++; counts.live = live.size;
            counts.peak = Math.max(counts.peak, counts.live);
        }
        return value;
    };
    WebGL2RenderingContext.prototype['delete' + kind] = function(value) {
        const result = destroy.call(this, value);
        if (live.delete(value)) { counts.deleted++; counts.live = live.size; }
        return result;
    };
}
"""


def run(client, directory, marker, seconds, target_id, capture_depth):
    """Observe API handles, not VRAM residency; readbacks make this a correctness run."""
    result = dict(schema="21kb.web-graphics-stability/v1", result="RUNNING",
        browser=client.call("Browser.getVersion"), requestedSeconds=seconds, longAcceptance=False,
        samples=[], cycles=[], liveHandleBaselines={}, gpuMemoryBytes="unavailable")
    path = directory / "stability.json"

    def write():
        temporary = path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(result, indent=2), encoding="utf-8")
        temporary.replace(path)

    def check(condition, message):
        if not condition:
            raise RuntimeError(message)

    def state():
        value = client.evaluate("(() => {const g=window.kbRuntimeGraphics;"
            "const c=document.querySelector('#canvas');return {data:{...document.documentElement.dataset},"
            "output:document.querySelector('#output')?.value,losses:window.kbStabilityLosses,"
            "resources:window.kbGLResources,width:c.width,height:c.height,dpr:devicePixelRatio,"
            "focused:document.hasFocus(),visible:document.visibilityState==='visible',"
            "graphics:g ? {lost:g.context.isContextLost(),renderer:g.renderer,shaders:g.shaders,"
            "copies:g.copyCount,depthCopies:g.depthCopyCount,badCopies:g.incompleteCopyCount,"
            "badDepthCopies:g.incompleteDepthCopyCount} : null,"
            "display:window.kbDisplayReadbacks?.slice(-1)[0]};})()")
        check(not value["data"].get("kbError") and not value["losses"],
            "Sustained rendering lost graphics or reported an error: " + str(value))
        check(value["focused"] and value["visible"], "The running game tab is not actually focused and visible")
        graphics = value["graphics"]
        check(graphics and not graphics["lost"] and graphics["shaders"] and
            all(shader["compiled"] for shader in graphics["shaders"]), "Actual graphics or shaders failed")
        check(not any(word in graphics["renderer"].lower() for word in
            ["swiftshader", "llvmpipe", "softpipe", "microsoft basic", "warp", "software"]),
            "Sustained rendering requires actual hardware")
        check(graphics["copies"] > 0 and graphics["depthCopies"] > 0 and
            graphics["badCopies"] == 0 and graphics["badDepthCopies"] == 0,
            "Sustained rendering used an incomplete framebuffer")
        check(value["display"] and value["display"][3] == 255, "Display alpha stopped being opaque")
        check(not any(event.get("method") == "Runtime.exceptionThrown" for event in client.events),
            "Sustained rendering produced an unhandled exception")
        frames = re.findall(re.escape(marker) + r"-frame=(\d+)", value.pop("output") or "")
        check(frames, "The normal Lua frame heartbeat is missing")
        value["scriptFrame"] = int(frames[-1])
        for kind, counts in value["resources"].items():
            check(counts["created"] - counts["deleted"] == counts["live"],
                "Observed graphics handles are inconsistent: " + kind)
        check(all(value["resources"][kind]["created"] > 0 for kind in
            ["Buffer", "Framebuffer", "Program", "Shader", "Texture"]) and
            value["resources"]["Framebuffer"]["deleted"] > 0,
            "The observer did not see actual GPU resource creation and destruction")
        return value

    def settle(duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            state()
            time.sleep(min(0.2, max(0, deadline - time.monotonic())))

    def resize(cycle):
        width, height, dpr = (960, 540, 2) if cycle % 2 else (800, 600, 1)
        client.call("Emulation.setDeviceMetricsOverride", width=1100, height=900,
            deviceScaleFactor=dpr, mobile=False)
        client.evaluate("Object.assign(document.querySelector('#canvas').style,"
            "{width:'" + str(width) + "px',height:'" + str(height) + "px'})")
        settle(2)
        value = state()
        check(value["width"] == width * dpr and value["height"] == height * dpr,
            "The sustained test's actual backbuffer did not resize")
        return value

    def focus_cycle():
        other = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        try:
            client.call("Target.activateTarget", targetId=other)
            check(client.evaluate("!document.hasFocus() && document.visibilityState==='hidden'"),
                "The owned tab did not cause actual focus loss")
            time.sleep(0.05)
            client.call("Target.activateTarget", targetId=target_id)
            client.call("Page.bringToFront")
        finally:
            client.call("Target.closeTarget", targetId=other)
        deadline = time.monotonic() + 3
        while not client.evaluate("document.hasFocus() && document.visibilityState==='visible'"):
            check(time.monotonic() < deadline, "The actual game tab did not regain focus")
            time.sleep(0.05)

    try:
        resize(0)
        settle(3)
        previous = state()["scriptFrame"]
        started = time.monotonic()
        next_cycle = started + min(60, max(1, seconds / 5))
        next_sample = started
        cycle = 0
        while True:
            now = time.monotonic()
            if now >= next_cycle and now - started < seconds:
                cycle += 1
                focus_cycle()
                value = resize(cycle)
                result["cycles"].append(dict(elapsedSeconds=time.monotonic() - started,
                    width=value["width"], height=value["height"], dpr=value["dpr"]))
                next_cycle = time.monotonic() + min(60, max(1, seconds / 5))
                now = time.monotonic()
            if now >= next_sample or now - started >= seconds:
                value = state()
                deadline = time.monotonic() + 5
                while value["scriptFrame"] <= previous:
                    check(time.monotonic() < deadline, "The running scene's frame heartbeat stopped")
                    time.sleep(0.1)
                    value = state()
                previous = value["scriptFrame"]
                mode = f"{value['width']}x{value['height']}@{value['dpr']}"
                live = {kind:counts["live"] for kind, counts in value["resources"].items()}
                baseline = result["liveHandleBaselines"].setdefault(mode, live)
                check(all(live[kind] <= baseline[kind] for kind in live),
                    "Live graphics handles grew after warmup: " + str((mode, baseline, live)))
                value["depthPixels"] = capture_depth(client, directory, "stable-" + str(len(result["samples"])))
                value["javascriptHeap"] = client.call("Runtime.getHeapUsage")
                value["elapsedSeconds"] = time.monotonic() - started
                result["samples"].append(value)
                result["elapsedSeconds"] = value["elapsedSeconds"]
                write()
                print("Graphics stability: " + str(round(value["elapsedSeconds"], 1)) +
                    " seconds, Lua frame " + str(previous), flush=True)
                next_sample = time.monotonic() + min(10, max(0.2, seconds / 10))
            if time.monotonic() - started >= seconds:
                break
            time.sleep(0.05)
        result.update(result="PASS", elapsedSeconds=time.monotonic() - started,
            longAcceptance=seconds >= 1800, final=state())
    except Exception as error:
        result.update(result="FAIL", error=str(error))
        raise
    finally:
        write()
    return result

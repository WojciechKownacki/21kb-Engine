"""Read actual native mixer PCM after trusted browser input and audio transport."""
from pathlib import Path
import json
import math
import shutil
import time
import urllib.request

from run_game_audio_property_tests import write_tone
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import RuntimeClient, require


# Observe standard WebAudio output after the backend's synchronous handler has
# filled it. Never generate samples, invoke the callback or resume the context.
AUDIO_OBSERVER = """
window.kbAudioReadback = {contexts:[],blocks:[],count:0};
const createProcessor = AudioContext.prototype.createScriptProcessor;
AudioContext.prototype.createScriptProcessor = function(...args) {
    const node = createProcessor.apply(this,args);
    const context = this;
    if (!kbAudioReadback.contexts.includes(context)) kbAudioReadback.contexts.push(context);
    queueMicrotask(() => node.addEventListener('audioprocess', event => {
        const samples = event.outputBuffer.getChannelData(0);
        let energy=0,peak=0,crossings=0;
        for (let i=0;i<samples.length;i++) {
            const sample=samples[i];
            energy+=sample*sample; peak=Math.max(peak,Math.abs(sample));
            if (i && samples[i-1] <= 0 && sample > 0) crossings++;
        }
        const blocks=kbAudioReadback.blocks;
        blocks.push({index:++kbAudioReadback.count,rms:Math.sqrt(energy/samples.length),peak,
            frequency:crossings*event.outputBuffer.sampleRate/samples.length,
            sampleRate:event.outputBuffer.sampleRate,frames:samples.length,
            contextState:context.state,timeMs:performance.now()});
        if (blocks.length>128) blocks.shift();
    }));
    return node;
};
window.kbReadAudio = () => ({count:kbAudioReadback.count,blocks:kbAudioReadback.blocks,
    contexts:kbAudioReadback.contexts.map(c => ({state:c.state,sampleRate:c.sampleRate})),
    activation:{active:navigator.userActivation.isActive,ever:navigator.userActivation.hasBeenActive}});
"""


def author_fixture(args, root, *, autoplay=False, script_name="AudioFlow", source_clip=None):
    tone = root / ("Tone" + (source_clip.suffix if source_clip else ".wav"))
    if source_clip is None:
        write_tone(tone)
    else:
        shutil.copy2(source_clip, tone)

    def configure(run, component, project):
        run(args.cli, "import", "--project", project, "--destination", "/Game/Audio", tone)
        component("CanvasRoot/SaveButton", "Audio Source", "clipAssetId=/Game/Audio/Tone.21kb",
            "volume=0.25", "pitch=1", "loop=true", "spatial=false", "autoplay=" + str(autoplay).lower())
        component("MainCamera", "Audio Listener", "localUser=0", "primary=true")

    return author_storage(args, root, script_name=script_name,
        script_source=Path(__file__).with_name("fixtures") / (script_name + ".lua"), configure=configure)


class AudioScenario(StorageScenario):
    marker = "audio-flow"

    def audio(self):
        return self.client.evaluate("kbReadAudio()")

    def output(self, label, after, sounding, reference=None, *, volume=0.25, frequency=440):
        deadline = time.monotonic() + 8
        while True:
            self.states()
            value = self.audio()
            blocks = [b for b in value["blocks"] if b["index"] > after][-4:]
            if len(blocks) == 4:
                if sounding:
                    expected_rms = (3276 / 32768) * volume / math.sqrt(2)
                    passed = all(b["contextState"] == "running" and abs(b["rms"] / expected_rms - 1) < 0.02 and
                        abs(b["frequency"] - frequency) < 30 for b in blocks)
                    if reference is not None:
                        passed = passed and all(abs(b["rms"] / reference - 1) < 0.1 for b in blocks)
                else:
                    passed = all(b["peak"] < 0.00001 for b in blocks)
                if passed:
                    self.report["observations"].append(dict(check=label, audio=value, settledBlocks=blocks))
                    return sum(b["rms"] for b in blocks) / 4
            require(time.monotonic() < deadline, label + ": " + str(value))
            time.sleep(0.03)

    def interrupt(self):
        # Inject through the public context API after activation. This qualifies
        # controlled suspension, not an OS/device interruption.
        self.client.call("Runtime.evaluate", expression="kbAudioReadback.contexts.at(-1).suspend()", awaitPromise=True)
        suspended_at = time.monotonic()
        count = self.audio()["count"]
        self.wait("native reports interrupted output and retains source intent",
            lambda state: not state["available"] and state["deviceStatus"] == 4 and state["playing"])
        require(self.audio()["count"] == count, "Output continued after context suspension")
        elapsed = (time.monotonic() - suspended_at) * 1000
        self.report.setdefault("suspensionDetectionsMs", []).append(elapsed)
        require(elapsed < 1500, "Native audio suspension diagnostic arrived too late")

    def restart(self, clicks, reference):
        cursor = len(self.states())
        after = self.audio()["count"]
        self.click_save()
        self.wait("native explicit audio restart " + str(clicks), lambda state: state["clicks"] == clicks and
            state["playing"] and state["available"], cursor)
        self.output("actual PCM after explicit restart " + str(clicks), after, True, reference)
        contexts = self.audio()["contexts"]
        require(len(contexts) == clicks - 4 and contexts[-1]["state"] == "running" and
            all(c["state"] == "closed" for c in contexts[:-1]), "Audio restart leaked a context: " + str(contexts))

    def focus_cycle(self, profile, reference):
        client = self.client
        target_id = client.call("Target.createTarget", url="about:blank", background=False)["targetId"]
        other = None
        try:
            port = int((profile / "DevToolsActivePort").read_text().splitlines()[0])
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/json/list", timeout=2) as response:
                target = next(t for t in json.load(response) if t["id"] == target_id)
            other = RuntimeClient(target["webSocketDebuggerUrl"])
            other.call("Page.enable")
            other.call("Page.bringToFront")
            require(client.evaluate("!document.hasFocus() && document.visibilityState === 'hidden'"),
                "Own second tab did not hide the player")
            self.output("actual PCM while player tab is hidden", self.audio()["count"], True, reference)
            require(client.evaluate("!document.hasFocus() && document.visibilityState === 'hidden'"),
                "Player did not remain hidden during audio measurement")
            client.call("Page.bringToFront")
        finally:
            try:
                client.call("Target.closeTarget", targetId=target_id)
            finally:
                if other is not None:
                    other.close()
        require(client.evaluate("document.hasFocus() && document.visibilityState === 'visible'"),
            "Player focus did not return")
        self.output("actual PCM after player focus returns", self.audio()["count"], True, reference)
        self.blue_button("audio-after-focus-native-ui")


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "audio-flow",
            LOAD_OBSERVER + AUDIO_OBSERVER, scenario_type=AudioScenario) as case:
        initial = case.ready()
        before = case.audio()
        report["audioBeforeGesture"] = dict(native=initial, webAudio=before)
        require(not initial["playing"] and before["count"] == 0, "Audio ran before browser activation")
        require(not before["activation"]["ever"], "Fresh page already has user activation")
        require(before["contexts"] and all(c["state"] == "suspended" for c in before["contexts"]),
            "Expected browser activation to gate the actual audio context: " + str(before))
        require(not initial["available"] and initial["deviceStatus"] == 4,
            "Native audio must report suspended output before activation: " + str(initial))
        reference = None
        for clicks, sounding, label in [(1, True, "play"), (2, False, "pause"),
                (3, True, "resume"), (4, False, "stop"), (5, True, "play again")]:
            cursor = len(case.states())
            after = case.audio()["count"]
            case.click_save()
            case.wait("native audio " + label, lambda state: state["clicks"] == clicks and
                state["playing"] == sounding and state["available"], cursor)
            measured = case.output("actual PCM " + label, after, sounding, reference if sounding else None)
            if clicks == 1:
                reference = measured
        require(len(case.audio()["contexts"]) == 1, "Audio transport created duplicate contexts")
        case.interrupt()
        case.restart(6, reference)
        case.focus_cycle(root / "profile", reference)
        case.interrupt()
        case.restart(7, reference)
        events = case.client.evaluate("kbMouseEvents")
        require(len([e for e in events if e["type"] == "click"]) == 7 and all(e["trusted"] for e in events),
            "Audio transport must come from trusted browser input: " + str(events))
        case.blue_button("audio-transport-native-ui")
        report["coverage"] = dict(nativeMixerPcm=True, trustedActivation=True, pauseResumeStop=True,
            suspendedOutputStatus=True, injectedContextSuspension=True,
            explicitBackendRecovery=True, desktopTabFocus=True,
            physicalSpeaker=False, deviceRouting=False, osInterruption=False, automaticInterruptionRecovery=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio/v1", description=__doc__, prefix="AudioFlow"))

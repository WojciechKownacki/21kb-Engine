"""Read actual native mixer PCM after trusted browser input and audio transport."""
from pathlib import Path
import math
import time

from run_game_audio_property_tests import write_tone
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import require


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


def author_fixture(args, root):
    tone = root / "Tone.wav"
    write_tone(tone)

    def configure(run, component, project):
        run(args.cli, "import", "--project", project, "--destination", "/Game/Audio", tone)
        component("CanvasRoot/SaveButton", "Audio Source", "clipAssetId=/Game/Audio/Tone.21kb",
            "volume=0.25", "pitch=1", "loop=true", "spatial=false", "autoplay=false")
        component("MainCamera", "Audio Listener", "localUser=0", "primary=true")

    return author_storage(args, root, script_name="AudioFlow",
        script_source=Path(__file__).with_name("fixtures") / "AudioFlow.lua", configure=configure)


class AudioScenario(StorageScenario):
    marker = "audio-flow"

    def audio(self):
        return self.client.evaluate("kbReadAudio()")

    def output(self, label, after, sounding, reference=None):
        deadline = time.monotonic() + 8
        while True:
            self.states()
            value = self.audio()
            blocks = [b for b in value["blocks"] if b["index"] > after][-4:]
            if len(blocks) == 4:
                if sounding:
                    expected_rms = (3276 / 32768) * 0.25 / math.sqrt(2)
                    passed = all(b["contextState"] == "running" and abs(b["rms"] / expected_rms - 1) < 0.02 and
                        abs(b["frequency"] - 440) < 30 for b in blocks)
                    if reference is not None:
                        passed = passed and all(abs(b["rms"] / reference - 1) < 0.1 for b in blocks)
                else:
                    passed = all(b["peak"] < 0.00001 for b in blocks)
                if passed:
                    self.report["observations"].append(dict(check=label, audio=value, settledBlocks=blocks))
                    return sum(b["rms"] for b in blocks) / 4
            require(time.monotonic() < deadline, label + ": " + str(value))
            time.sleep(0.03)


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
        events = case.client.evaluate("kbMouseEvents")
        require(len([e for e in events if e["type"] == "click"]) == 5 and all(e["trusted"] for e in events),
            "Audio transport must come from trusted browser input: " + str(events))
        # Inject an interruption through the standard API after activation. This
        # does not unlock audio or qualify recovery from an OS/device interruption.
        case.client.call("Runtime.evaluate", expression="kbAudioReadback.contexts[0].suspend()", awaitPromise=True)
        suspended_at = time.monotonic()
        count = case.audio()["count"]
        case.wait("native reports interrupted output and retains source intent",
            lambda state: not state["available"] and state["deviceStatus"] == 4 and state["playing"])
        require(case.audio()["count"] == count, "Output continued after context suspension")
        report["suspensionDetectionMs"] = (time.monotonic() - suspended_at) * 1000
        require(report["suspensionDetectionMs"] < 1500, "Native audio suspension diagnostic arrived too late")
        case.blue_button("audio-transport-native-ui")
        report["coverage"] = dict(nativeMixerPcm=True, trustedActivation=True, pauseResumeStop=True,
            suspendedOutputStatus=True, injectedContextSuspension=True,
            physicalSpeaker=False, deviceRouting=False, backgroundInterruption=False, interruptionRecovery=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio/v1", description=__doc__, prefix="AudioFlow"))

"""Author audio properties through the CLI and read them from the cooked native game."""
from pathlib import Path
import math
import struct
import wave

from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, StorageScenario, author_fixture as author_storage, main
from run_game_touch_tests import require


def write_tone(tone):
    with wave.open(str(tone), "wb") as audio:
        audio.setparams((1, 2, 48000, 0, "NONE", "not compressed"))
        audio.writeframes(b"".join(struct.pack("<h", round(3276 * math.sin(2 * math.pi * 440 * frame / 48000)))
            for frame in range(48000)))


def author_fixture(args, root):
    tone = root / "Tone.wav"
    write_tone(tone)

    def configure(run, component, project):
        run(args.cli, "import", "--project", project, "--destination", "/Game/Audio", tone)
        component("CanvasRoot/SaveButton", "Audio Source", "clipAssetId=/Game/Audio/Tone.21kb", "volume=0.25",
            "pitch=1.25", "loop=true", "spatial=false", "autoplay=false", "pan=0.2", "spatialBlend=0.5",
            "attenuationModel=Linear", "minDistance=2", "maxDistance=20", "rolloff=0.5", "dopplerFactor=1.2")
        component("CanvasRoot/SaveButton", "Audio Listener", "priority=7", "localUser=3", "primary=false")

    return author_storage(args, root, script_name="AudioProperties",
        script_source=Path(__file__).with_name("fixtures") / "AudioProperties.lua", configure=configure)


class AudioPropertyScenario(StorageScenario):
    marker = "audio-properties"


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "audio-properties", LOAD_OBSERVER,
            scenario_type=AudioPropertyScenario) as case:
        state = case.ready()
        for name, expected in dict(authoredVolume=0.25, authoredPitch=1.25, volume=0.5, pitch=1,
                pan=0.2, spatialBlend=0.5, minDistance=2, maxDistance=20, rolloff=0.5, dopplerFactor=1.2).items():
            require(abs(state[name] - expected) < 0.0001, "Cooked audio property differs: " + name + " " + str(state))
        require(state["attenuation"] == 2 and state["loop"] and not state["spatial"] and not state["autoplay"] and
            not state["mute"] and state["enabled"] and state["listenerPriority"] == 7 and state["localUser"] == 3 and
            not state["listenerPrimary"] and state["listenerEnabled"], "Cooked audio controls differ: " + str(state))
        case.blue_button("audio-property-native-ui")
        report["observations"].append(dict(check="CLI authored audio properties survive cook and native script mutation", state=state))
        report["coverage"] = dict(audioPropertyAuthoring=True, nativeScript=True, gpuRendering=True, audioOutput=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio-properties/v1", description=__doc__, prefix="AudioProperties"))

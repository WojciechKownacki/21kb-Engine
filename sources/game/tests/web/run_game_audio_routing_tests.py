"""Verify CLI-authored bus hierarchy and live mixer changes through native PCM."""
from run_game_audio_tests import AUDIO_OBSERVER, AudioScenario, author_fixture as author_audio
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


def author_fixture(args, root):
    def configure(run, component, project):
        run(args.cli, "audio-mixer", "new", "--project", project, "--name", "Main",
            "--bus", "Output", "--bus", "Effects", "--set", "Output.volume=0.5",
            "--set", "Effects.parent=Output", "--set", "Effects.volume=0.5")
        run(args.cli, "audio-mixer", "assign", "--project", project, "--mixer", "/Game/Audio/Main.kbmixer")
        run(args.cli, "component", "set", "--project", project, "--entity", "CanvasRoot/SaveButton",
            "--type", "Audio Source", "--set", "outputBus=Effects")
    return author_audio(args, root, autoplay=True, script_name="AudioRouting", configure_extra=configure)


class RoutingScenario(AudioScenario):
    marker = "audio-routing"


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "audio-routing",
            LOAD_OBSERVER + AUDIO_OBSERVER, scenario_type=RoutingScenario) as case:
        initial = case.ready()
        audio = case.audio()
        report["audioBeforeGesture"] = dict(native=initial, webAudio=audio)
        require(initial["assigned"] and initial["playing"] and not initial["available"] and
            initial["deviceStatus"] == 4 and not audio["activation"]["ever"] and audio["count"] == 0,
            "Cooked scene did not retain its routed autoplay before activation")
        phases = [("authored parent and child gain", True, 0.0625),
            ("parent gain override", True, 0.125), ("child gain override", True, 0.25),
            ("clear parent override", True, 0.125), ("clear child override", True, 0.0625),
            ("remove active mixer", False, 0), ("restore active mixer", True, 0.0625)]
        for clicks, (label, sounding, volume) in enumerate(phases, 1):
            cursor = len(case.states())
            after = case.audio()["count"]
            case.click_save()
            state = case.wait("native " + label, lambda v: v["clicks"] == clicks and v["available"] and
                v["playing"] == sounding and v["assigned"] == (clicks != 6), cursor)
            if clicks == 6:
                require(state["refusedStatus"] == 9, "Missing mixer did not explicitly reject named routing")
            case.output("actual PCM " + label, after, sounding, volume=volume)
        events = case.client.evaluate("kbMouseEvents")
        require(len([e for e in events if e["type"] == "click"]) == 7 and all(e["trusted"] for e in events),
            "Mixer changes require trusted native UI clicks")
        require(len(case.audio()["contexts"]) == 1, "Bus or mixer changes recreated the audio context")
        case.blue_button("audio-routing-native-ui")
        report["coverage"] = dict(cliMixerAuthoring=True, cookedSceneAssignment=True, actualNativePcm=True,
            parentChildProduct=True, liveBusOverrides=True, clearBusOverrides=True,
            missingMixerRefusal=True, mixerReassignment=True, editorMixComparison=False, physicalOutput=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio-routing/v1", description=__doc__, prefix="AudioRouting"))

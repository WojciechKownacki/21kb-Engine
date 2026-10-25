"""Verify CLI-authored snapshots, overrides and fades through real packaged PCM."""
from run_game_audio_tests import AUDIO_OBSERVER, AudioScenario, author_fixture as author_audio
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


def author_fixture(args, root):
    def configure(run, component, project):
        run(args.cli, "audio-mixer", "new", "--project", project, "--name", "Main",
            "--bus", "Output", "--bus", "Effects", "--set", "Output.volume=0.5",
            "--set", "Effects.parent=Output", "--set", "Effects.volume=0.5",
            "--add-snapshot", "Quiet", "--add-snapshot", "Loud",
            "--snapshot", "Quiet", "--set-snapshot", "Effects=0.125")
        run(args.cli, "audio-mixer", "set", "--project", project, "--mixer", "/Game/Audio/Main.kbmixer",
            "--snapshot", "Loud", "--set-snapshot", "Effects=1")
        run(args.cli, "audio-mixer", "assign", "--project", project, "--mixer", "/Game/Audio/Main.kbmixer",
            "--snapshot", "Quiet")
        run(args.cli, "component", "set", "--project", project, "--entity", "CanvasRoot/SaveButton",
            "--type", "Audio Source", "--set", "outputBus=Effects")
    return author_audio(args, root, autoplay=True, script_name="AudioSnapshots", configure_extra=configure)


class SnapshotScenario(AudioScenario):
    marker = "audio-snapshots"


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "audio-snapshots",
            LOAD_OBSERVER + AUDIO_OBSERVER, scenario_type=SnapshotScenario) as case:
        initial = case.ready()
        audio = case.audio()
        report["audioBeforeGesture"] = dict(native=initial, webAudio=audio)
        require(initial["snapshot"] == "Quiet" and initial["playing"] and not initial["available"] and
            initial["deviceStatus"] == 4 and not audio["activation"]["ever"] and audio["count"] == 0,
            "Cooked snapshot autoplay did not remain gated by activation")
        phases = [("authored quiet snapshot", "Quiet", 0.015625), ("loud snapshot", "Loud", 0.125),
            ("runtime override", "Loud", 0.0625), ("snapshot under runtime override", "Quiet", 0.0625),
            ("clear runtime override", "Quiet", 0.015625), ("authored base mix", "", 0.0625),
            ("fade to quiet", "Quiet", 0.015625), ("fade to base", "", 0.0625)]
        reference = base_reference = None
        for clicks, (label, snapshot, volume) in enumerate(phases, 1):
            cursor = len(case.states())
            after = case.audio()["count"]
            case.click_save()
            case.wait("native " + label, lambda v: v["clicks"] == clicks and v["available"] and
                v["playing"] and v["snapshot"] == snapshot, cursor)
            measured = case.output("actual PCM " + label, after, True, volume=volume)
            if clicks == 1:
                reference = measured
            if clicks == 6:
                base_reference = measured
            if clicks in (7, 8):
                blocks = [b for b in case.audio()["blocks"] if b["index"] > after]
                middle = [b for b in blocks if reference * 1.1 < b["rms"] < reference * 4 * 0.9]
                require(len(middle) >= 3, "Fade did not produce enough intermediate native PCM blocks")
                increasing = clicks == 8
                require(all((b["rms"] >= a["rms"] * 0.95 if increasing else b["rms"] <= a["rms"] * 1.05)
                    for a, b in zip(middle, middle[1:])), "Native PCM fade did not move monotonically")
                report["observations"].append(dict(check="actual PCM ramp " + label, intermediateBlocks=middle))
        cursor = len(case.states())
        after = case.audio()["count"]
        case.click_save()
        case.wait("native interrupted and overlapping fades complete", lambda v: v["clicks"] == 9 and
            v["retargetStage"] == 4 and v["available"] and v["playing"] and v["snapshot"] == "", cursor)
        case.output("actual PCM after overlapping fades", after, True, reference=base_reference, volume=0.0625)
        blocks = [b for b in case.audio()["blocks"] if b["index"] > after]
        require(len(blocks) >= 16 and all(base_reference * 0.75 < b["rms"] < base_reference * 1.3 for b in blocks),
            "Interrupted fades jumped outside the continuous blend envelope: " + str(blocks))
        report["observations"].append(dict(check="actual PCM continuity across interrupted and overlapping fades", blocks=blocks))
        events = case.client.evaluate("kbMouseEvents")
        require(len([e for e in events if e["type"] == "click"]) == len(phases) + 1 and all(e["trusted"] for e in events),
            "Snapshot changes require trusted native UI clicks")
        require(len(case.audio()["contexts"]) == 1, "Snapshot changes recreated the audio context")
        case.blue_button("audio-snapshots-native-ui")
        report["coverage"] = dict(cliSnapshotAuthoring=True, cookedSnapshotSelection=True, actualNativePcm=True,
            snapshotSwitch=True, overridePrecedence=True, returnToBase=True, transitionRamp=True,
            transitionInterruption=True, overlappingTransitions=True, retargetContinuity=True, preciseCurveTiming=False,
            editorMixComparison=False, physicalOutput=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio-snapshots/v1", description=__doc__, prefix="AudioSnapshots"))

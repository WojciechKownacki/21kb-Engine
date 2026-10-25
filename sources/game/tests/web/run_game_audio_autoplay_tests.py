"""Verify queued native autoplay and live source properties through real PCM."""
from run_game_audio_tests import AUDIO_OBSERVER, AudioScenario, author_fixture as author_audio
from run_game_loading_tests import LOAD_OBSERVER
from run_game_storage_tests import BrowserSession, main
from run_game_touch_tests import require


def author_fixture(args, root):
    return author_audio(args, root, autoplay=True, script_name="AudioAutoplay")


class AutoplayScenario(AudioScenario):
    marker = "audio-autoplay"


def run_cases(args, root, report, origin):
    with BrowserSession(args, root, root / "profile", report, origin, "audio-autoplay",
            LOAD_OBSERVER + AUDIO_OBSERVER, scenario_type=AutoplayScenario) as case:
        initial = case.ready()
        audio = case.audio()
        report["audioBeforeGesture"] = dict(native=initial, webAudio=audio)
        require(initial["autoplay"] and initial["playing"] and not initial["available"] and initial["deviceStatus"] == 4,
            "Authored autoplay intent was not queued before browser activation: " + str(initial))
        require(not audio["activation"]["ever"] and audio["count"] == 0 and audio["contexts"] and
            all(c["state"] == "suspended" for c in audio["contexts"]), "Audio ran before activation: " + str(audio))
        phases = [("unlock queued autoplay", True, True, 0.25, 440, False),
            ("live gain", True, True, 0.5, 440, False), ("mute", False, True, 0.5, 440, True),
            ("unmute", True, True, 0.5, 440, False), ("live pitch", True, True, 0.5, 880, False),
            ("disable", False, False, 0.5, 880, False), ("enable autoplay", True, True, 0.5, 880, False)]
        for clicks, (label, sounding, enabled, volume, frequency, mute) in enumerate(phases, 1):
            cursor = len(case.states())
            after = case.audio()["count"]
            case.click_save()
            state = case.wait("native " + label, lambda v: v["clicks"] == clicks and v["available"] and
                v["playing"] == enabled and v["enabled"] == enabled and v["mute"] == mute and
                abs(v["volume"] - volume) < 0.0001 and abs(v["pitch"] - frequency / 440) < 0.0001, cursor)
            require(state["autoplay"], "A live property update cleared authored autoplay intent")
            case.output("actual PCM " + label, after, sounding, volume=volume, frequency=frequency)
        events = case.client.evaluate("kbMouseEvents")
        require(len([e for e in events if e["type"] == "click"]) == 7 and all(e["trusted"] for e in events),
            "Autoplay unlock and source properties require trusted native UI input")
        require(len(case.audio()["contexts"]) == 1, "Live source properties recreated the audio context")
        case.blue_button("audio-autoplay-native-ui")
        report["coverage"] = dict(queuedAutoplay=True, nativePcm=True, liveGain=True, mute=True,
            livePitch=True, enableAutoplay=True, explicitPlayRequested=False, physicalOutput=False)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio-autoplay/v1", description=__doc__, prefix="AudioAutoplay"))

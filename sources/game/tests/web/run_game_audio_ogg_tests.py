"""Import, cook and measure Ogg Vorbis playback through the real native player."""
from pathlib import Path

from run_game_audio_autoplay_tests import run_cases
from run_game_audio_tests import author_fixture as author_audio
from run_game_storage_tests import main


def author_fixture(args, root):
    tone = Path(__file__).resolve().parents[3] / "plugins/audio_miniaudio/tests/assets/tone.ogg"
    return author_audio(args, root, autoplay=True, script_name="AudioAutoplay", source_clip=tone)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-audio-ogg/v1", description=__doc__, prefix="AudioOgg"))

"""Author the native Android audio lifecycle fixture using the CLI."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "web"))
from run_game_audio_property_tests import write_tone
from run_game_storage_tests import author_fixture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "cooker", "engine-root", "shaderc", "root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cook-cache", type=Path)
    parser.add_argument("--target", choices=("Android.ASTC.arm64", "Android.ETC2.arm64"), default="Android.ASTC.arm64")
    args = parser.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path): setattr(args, name, value.resolve())
    args.root.mkdir(parents=True, exist_ok=True)
    tone = args.root / "Tone.wav"
    write_tone(tone)

    def configure(run, component, project):
        run(args.cli, "import", "--project", project, "--destination", "/Game/Audio", tone)
        component("CanvasRoot/SaveButton", "Audio Source", "clipAssetId=/Game/Audio/Tone.21kb",
            "volume=0.25", "loop=true", "spatial=false", "autoplay=false")
        component("MainCamera", "Audio Listener", "localUser=0", "primary=true")

    author_fixture(args, args.root, target=args.target, script_name="AudioLifecycle",
        script_source=Path(__file__).with_name("fixtures") / "AudioLifecycle.lua", configure=configure)


if __name__ == "__main__":
    main()

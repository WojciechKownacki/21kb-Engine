"""Author and cook the shared UI/save fixture for an Android lifecycle test APK."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "web"))
from run_game_storage_tests import author_fixture


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "cooker", "engine-root", "shaderc", "root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cook-cache", type=Path)
    parser.add_argument("--target", choices=("Android.ASTC.arm64", "Android.ETC2.arm64"), default="Android.ASTC.arm64")
    args = parser.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    args.root.mkdir(parents=True, exist_ok=True)
    author_fixture(args, args.root, target=args.target)

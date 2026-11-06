"""Cook a controller action scene for the production Android packaged player."""
import argparse
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "web"))
from run_game_storage_tests import author_fixture


def asset_id(key):
    value = 14695981039346656037
    for byte in key.encode("utf-8"):
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value or 1099511628211


def configure(run, component, project):
    # Use the public preset to register the project's startup mapping context.
    run(args.cli, "entity", "add", "--project", project,
        "--create", "Top Down Player", "--name", "Player", "--physics", "2d")
    directory = project / "Assets/Input"
    fields = [("LX", 340), ("LY", 341), ("RX", 342), ("RY", 343),
        ("LT", 344), ("RT", 345), ("A", 300), ("B", 301),
        ("Up", 304), ("Down", 305), ("Left", 306), ("Right", 307)]
    # InputAssetIO v3 stores explicit per-binding controller indices. The cooker
    # loads and validates these fixture assets using the production decoder.
    mappings = []
    for slot in range(4):
        for field, key in fields:
            name = f"Pad{slot}{field}"
            filename = name + ".21kbinputaction"
            encoded = name.encode("utf-8")
            (directory / filename).write_bytes(b"21KBIAC\0" +
                struct.pack("<II", 3, len(encoded)) + encoded + bytes([1, 0]))
            identity = asset_id("/Game/Input/" + filename + ":InputAction")
            mappings.append(struct.pack("<QQIfBII", len(mappings) + 1, identity, key, 1.0, slot, 0, 0))
    (directory / "PlayerInput.21kbinputcontext").write_bytes(b"21KBIMC\0" +
        struct.pack("<II", 3, len(mappings)) + b"".join(mappings) + struct.pack("<I", 0))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "cooker", "engine-root", "shaderc", "root"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--cook-cache", type=Path)
    parser.add_argument("--target", choices=("Android.ASTC.arm64", "Android.ETC2.arm64"),
        default="Android.ASTC.arm64")
    args = parser.parse_args()
    for name, value in vars(args).items():
        if isinstance(value, Path):
            setattr(args, name, value.resolve())
    args.root.mkdir(parents=True, exist_ok=True)
    author_fixture(args, args.root, target=args.target, script_name="ControllerFlow",
        script_source=Path(__file__).with_name("ControllerFlow.lua"), configure=configure)

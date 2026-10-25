"""Measure loading a cooked texture workload and verify its actual GPU pixels."""
from pathlib import Path
import random
import struct
import zlib

from run_game_loading_tests import LOAD_OBSERVER, measure
from run_game_storage_tests import BrowserSession, author_fixture as author_storage_fixture, main
from run_game_storage_delete_tests import verify
from run_game_touch_tests import require


def write_texture(project):
    width = height = 2048
    pixels = bytearray(random.Random(0x21B).randbytes(width * height * 3))
    # Distinct corner patches let GPU readback verify the cooked texture and orientation.
    for x, y, color in [(0, 0, (255, 0, 0)), (1792, 0, (0, 255, 0)),
            (0, 1792, (0, 0, 255)), (1792, 1792, (255, 255, 0))]:
        row = bytes(color) * 256
        for line in range(y, y + 256):
            offset = (line * width + x) * 3
            pixels[offset:offset + len(row)] = row
    raw = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    source = project.parent / "Pattern.png"
    source.write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(raw, 1)) + chunk(b"IEND", b""))
    return source


def add_content(args, run, component, project):
    source = write_texture(project)
    run(args.cli, "import", "--project", project, "--destination", "/Game/Textures", source)
    run(args.cli, "entity", "add", "--project", project, "--name", "PatternImage", "--parent", "CanvasRoot")
    component("CanvasRoot/PatternImage", "Rect Transform", "offsetMin=300,80", "offsetMax=556,336")
    component("CanvasRoot/PatternImage", "Image", "imageAssetId=/Game/Textures/Pattern.21kb")


def author_fixture(args, root):
    def configure(run, component, project):
        add_content(args, run, component, project)
    return author_storage_fixture(args, root, script_name="StorageDelete",
        script_source=Path(__file__).with_name("fixtures") / "StorageDelete.lua", configure=configure)


def verify_content(case, label, report, pack_size):
    verify(case.ready(), 0)
    colors = case.picture(label + "-cooked-pattern", [[316, 96], [540, 96], [316, 320], [540, 320], [60, 100]])
    red, green, blue, yellow, button = colors
    require(red[0] > 180 and red[1] < 70 and red[2] < 70 and
        green[1] > 180 and green[0] < 70 and green[2] < 70 and
        blue[2] > 180 and blue[0] < 70 and blue[1] < 70 and
        yellow[0] > 180 and yellow[1] > 180 and yellow[2] < 70 and
        button[2] > button[0] + 50,
        "The real cooked texture pixels, orientation or native button differ: " + str(colors))
    measure(case, label, report, pack_size)
    report["observations"].append(dict(check="cooked texture and native button render", label=label, pixels=colors))


def run_cases(args, root, report, origin):
    pack_size = (root / "Web/Game.kbpack").stat().st_size
    require(pack_size >= 4 * 1024 * 1024, "The actual cooked workload package is too small: " + str(pack_size))
    report["workload"] = dict(textureWidth=2048, textureHeight=2048, packageBytes=pack_size)
    for label in ["cold", "warm"]:
        with BrowserSession(args, root, root / "profile", report, origin, label, LOAD_OBSERVER) as case:
            verify_content(case, label, report, pack_size)


if __name__ == "__main__":
    raise SystemExit(main(fixture=author_fixture, scenario=run_cases,
        schema="21kb.web-game-content-loading/v1", description=__doc__, prefix="ContentLoading"))

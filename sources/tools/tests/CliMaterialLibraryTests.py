#!/usr/bin/env python
"""Every ready material is drawn: none is blank or one flat colour (headless, real player).

All surface presets of the library are made in a new project and previewed side by side with `kb_cli material-preview`; each sphere
must show the shading of a lit sphere (not the empty background, not one flat colour). The Post Process presets must change the
picture of the neutral one, and the User Interface presets must fill their panel with a gradient. The graphs compile at make time
(material-new --preset refuses one that does not), so a picture here proves the shader cooked and drew as well.

Usage: python CliMaterialLibraryTests.py --cli <kb_cli.exe> [--keep <folder for the pictures>] (kb_game.exe lies beside kb_cli.exe)
"""
import argparse
import math
import pathlib
import shutil
import statistics
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3] / "scripts"))
from png_readback import read_png  # noqa: E402


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def run(tool, *args, expect=0):
    done = subprocess.run([str(tool), *map(str, args)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if done.returncode != expect:
        fail("%s %s exited %d:\n%s%s" % (tool.name, " ".join(map(str, args)), done.returncode, done.stdout[-1500:], done.stderr[-1500:]))
    return done.stdout + done.stderr


def pascal(name):
    return "".join(part[:1].upper() + part[1:] for part in name.split("-"))


def luminance(pixel):
    return 0.299 * pixel[0] + 0.587 * pixel[1] + 0.114 * pixel[2]


def disc(rows, cell, column, row):
    """The pixels inside the sphere of one cell, and the background around it (the corners of the cell)."""
    x0, y0 = column * cell, row * cell
    centre, radius = cell / 2.0, cell * 0.4
    inside, corners = [], []
    for y in range(cell):
        for x in range(cell):
            pixel = rows[y0 + y][x0 + x]
            if math.hypot(x + 0.5 - centre, y + 0.5 - centre) <= radius:
                inside.append(pixel)
            elif x < 4 and y < 4:
                corners.append(pixel)
    return inside, corners


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--keep", type=pathlib.Path)
    options = parser.parse_args()
    cli, keep = options.cli.resolve(strict=True), options.keep
    work = pathlib.Path(tempfile.mkdtemp(prefix="kb_material_library_"))
    try:
        run(cli, "new", "--parent", work, "--name", "Library")
        project = work / "Library"
        listing = run(cli, "guide", "--topic", "material-presets", "--project", project)
        families = {}
        for line in listing.splitlines():
            if line.startswith("#") or ": " not in line:
                continue
            family, names = line.split(": ", 1)
            families[family] = [name.strip() for name in names.split(",")]
        total = sum(len(names) for names in families.values())
        if total < 150:
            fail("the library lists %d presets, about 150 are expected" % total)

        for names in families.values():
            for name in names:
                run(cli, "material-new", "--project", project, "--preset", name)
        surface = [name for family, names in families.items() if family not in ("post", "ui") for name in names]

        sheet = work / "sheet.png"
        run(cli, "material-preview", "--project", project, "--out", sheet,
            *[part for name in surface for part in ("--material", "/Game/Materials/%s.kbmatinst" % pascal(name))])
        rows = read_png(sheet)
        columns = math.ceil(math.sqrt(len(surface)))
        cell = len(rows[0]) // columns
        problems = []
        for index, name in enumerate(surface):
            inside, corners = disc(rows, cell, index % columns, index // columns)
            background = [statistics.mean(pixel[channel] for pixel in corners) for channel in range(3)]
            spread = statistics.pstdev(luminance(pixel) for pixel in inside)
            apart = statistics.mean(sum(abs(pixel[channel] - background[channel]) for channel in range(3)) / 3.0 for pixel in inside)
            if spread < 2.0 or apart < 3.0:
                problems.append("%s: the sphere is %s (spread %.1f, %.1f from the background)" % (name, "flat" if spread < 2.0 else "empty", spread, apart))
        if problems:
            fail("these presets draw blank or flat:\n  " + "\n  ".join(problems))
        print("OK: %d surface presets drawn, each lit and not flat (%dx%d sheet)" % (len(surface), len(rows[0]), len(rows)))

        def mean_difference(left, right):
            total, count = 0.0, 0
            for row_left, row_right in zip(left, right):
                for a, b in zip(row_left, row_right):
                    total += sum(abs(a[channel] - b[channel]) for channel in range(3)) / 3.0
                    count += 1
            return total / count

        post = families.get("post", [])
        run(cli, "material-new", "--project", project, "--preset", "post-grayscale", "--name", "PostNeutral", "--set", "Saturation=1")
        neutral_file = work / "post_neutral.png"
        run(cli, "material-preview", "--project", project, "--material", "/Game/Materials/PostNeutral.kbmat", "--out", neutral_file)
        neutral = read_png(neutral_file)
        for name in post:
            picture = work / ("post_%s.png" % name)
            run(cli, "material-preview", "--project", project, "--material", "/Game/Materials/%s.kbmat" % pascal(name), "--out", picture)
            difference = mean_difference(neutral, read_png(picture))
            if difference < 3.0:
                fail("%s does not change the picture (%.1f from the neutral grade)" % (name, difference))
        print("OK: %d Post Process presets change the picture" % len(post))

        for name in families.get("ui", []):
            picture = work / ("ui_%s.png" % name)
            run(cli, "material-preview", "--project", project, "--material", "/Game/Materials/%s.kbmat" % pascal(name), "--out", picture)
            panel = read_png(picture)
            height, width = len(panel), len(panel[0])
            top, bottom = panel[int(height * 0.28)][width // 2], panel[int(height * 0.72)][width // 2]
            corner = panel[2][2]
            if sum(abs(top[c] - bottom[c]) for c in range(3)) < 12 or sum(abs(top[c] - corner[c]) for c in range(3)) < 12:
                fail("%s does not draw a gradient panel (top %s, bottom %s, background %s)" % (name, top, bottom, corner))
        print("OK: %d User Interface presets draw a gradient panel" % len(families.get("ui", [])))
    finally:
        if keep is not None:
            keep.mkdir(parents=True, exist_ok=True)
            for picture in work.glob("*.png"):
                shutil.copy2(picture, keep / picture.name)
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()

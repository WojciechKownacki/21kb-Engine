#!/usr/bin/env python
"""Outline, water and layered terrain are drawn: the pictures show what the materials promise (headless, real player).

- Outline: a sphere with a red outline of 6 pixels is ringed by a red band 4 to 9 pixels thick at every angle, touching the sphere.
- Through walls: the same sphere behind a wall changes the picture where it hides when HiddenStrength is raised, in the colour of the glow.
- Water: the preset draws on a computer (the full Single Layer Water) and on a phone (--device phone, lit translucent), and the two are not the same picture.
- Terrain layers: eight layers of one array texture show by the height and the slope of a terrain (at least five of the eight colours appear).

Usage: python CliMaterialEffectsTests.py --cli <kb_cli.exe> [--keep <folder for the pictures>] (kb_game.exe lies beside kb_cli.exe)
"""
import argparse
import math
import pathlib
import re
import shutil
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
        fail("%s %s exited %d:\n%s%s" % (tool.name, " ".join(map(str, args)), done.returncode, done.stdout[-1800:], done.stderr[-1800:]))
    return done.stdout + done.stderr


def is_red(pixel):
    return pixel[0] > 150 and pixel[1] < 90 and pixel[2] < 90


def distance(left, right):
    return sum(abs(left[channel] - right[channel]) for channel in range(3))


def test_outline(cli, project, work, keep):
    run(cli, "material-new", "--project", project, "--preset", "outline-black", "--name", "RedLine",
        "--set", "OutlineColor=#FF0000", "--set", "OutlineWidth=6", "--set", "Color=#E0E0E0")
    picture = work / "outline.png"
    run(cli, "material-preview", "--project", project, "--material", "/Game/Materials/RedLine.kbmatinst", "--out", picture)
    rows = read_png(picture)
    height, width = len(rows), len(rows[0])
    centre_x, centre_y = width / 2.0, height / 2.0
    background = rows[3][3]
    good, measured, thickness_sum = 0, 0, 0.0
    for degrees in range(0, 360, 5):
        angle = math.radians(degrees)
        red_first = red_last = body_last = None
        radius = 4.0
        while radius < min(width, height) / 2.0 - 2:
            x = int(centre_x + math.cos(angle) * radius)
            y = int(centre_y + math.sin(angle) * radius)
            pixel = rows[y][x]
            if is_red(pixel):
                red_first = radius if red_first is None else red_first
                red_last = radius
            elif distance(pixel, background) > 24 and red_first is None:
                body_last = radius
            radius += 0.5
        measured += 1
        if red_first is None or body_last is None:
            continue
        thickness = red_last - red_first + 0.5
        thickness_sum += thickness
        if 4.0 <= thickness <= 9.0 and abs(red_first - body_last) <= 3.5:
            good += 1
    if good < measured * 0.9:
        fail("the outline is not an even band of about 6 pixels touching the sphere: %d of %d directions are (mean thickness %.1f)" % (good, measured, thickness_sum / max(measured, 1)))
    print("OK: the outline is a red band about %.1f pixels thick round the sphere in %d of %d directions" % (thickness_sum / measured, good, measured))
    if keep is not None:
        shutil.copy2(picture, keep / "outline.png")


def new_scene(cli, project, name):
    run(cli, "scene-new", "--project", project, "--name", name, "--template", "Empty")
    run(cli, "entity", "add", "--project", project, "--scene", name, "--create", "Directional Light", "--name", "Sun", "--rotation", "40,-35,0")
    return name


def scene_with_wall(cli, project):
    scene = new_scene(cli, project, "WallTest")
    run(cli, "entity", "add", "--project", project, "--scene", scene, "--create", "Camera", "--name", "Eye", "--position", "0,0,-12")
    run(cli, "entity", "add", "--project", project, "--scene", scene, "--create", "Sphere", "--name", "Hero", "--position", "0,0,0")
    run(cli, "entity", "add", "--project", project, "--scene", scene, "--create", "Cube", "--name", "Wall", "--position", "0,0,-4", "--scale", "9,9,0.3", "--color", "#C8C8C8")
    return scene


def test_through_wall(cli, project, work, keep):
    scene = scene_with_wall(cli, project)
    pictures = []
    for name, strength in (("Plain", "0"), ("Glow", "1")):
        run(cli, "material-new", "--project", project, "--preset", "xray-ally", "--name", "Hero" + name, "--entity", "Hero", "--scene", scene,
            "--set", "HiddenColor=#0000FF", "--set", "HiddenStrength=" + strength, "--set", "Color=#E0E0E0")
        picture = work / ("wall_%s.png" % name.lower())
        run(cli, "capture", "--project", project, "--scene", scene, "--camera", "Eye", "--out", picture, "--frames", "30", "--width", "480", "--height", "480")
        pictures.append(read_png(picture))
    plain, glow = pictures
    height, width = len(plain), len(plain[0])
    changed, sum_x, sum_y, blue_gain = 0, 0, 0, 0
    for y in range(height):
        for x in range(width):
            if distance(plain[y][x], glow[y][x]) > 12:
                changed += 1
                sum_x += x
                sum_y += y
                blue_gain += (glow[y][x][2] - plain[y][x][2]) - (glow[y][x][0] - plain[y][x][0])
    if changed < 300:
        fail("the part of the sphere behind the wall shows no glow: only %d pixels changed" % changed)
    offset = math.hypot(sum_x / changed - width / 2.0, sum_y / changed - height / 2.0)
    if offset > 40 or blue_gain / changed < 15:
        fail("the glow is not blue round the sphere behind the wall (centre off by %.0f px, blue gain %.1f)" % (offset, blue_gain / changed))
    print("OK: the sphere behind a wall glows blue where it hides (%d pixels, centred within %.0f px)" % (changed, offset))
    if keep is not None:
        shutil.copy2(work / "wall_glow.png", keep / "wall_glow.png")
        shutil.copy2(work / "wall_plain.png", keep / "wall_plain.png")


def mean_difference(left, right):
    total, count = 0.0, 0
    for row_left, row_right in zip(left, right):
        for a, b in zip(row_left, row_right):
            total += distance(a, b) / 3.0
            count += 1
    return total / count


def test_water(cli, project, work, keep):
    run(cli, "material-new", "--project", project, "--preset", "water-lake", "--name", "Lake")
    pictures = {}
    for device in ("computer", "phone"):
        picture = work / ("water_%s.png" % device)
        run(cli, "material-preview", "--project", project, "--material", "/Game/Materials/Lake.kbmatinst", "--out", picture, "--device", device)
        pictures[device] = read_png(picture)
        rows = pictures[device]
        values = [sum(rows[y][x]) for y in range(len(rows) // 4, 3 * len(rows) // 4, 4) for x in range(len(rows[0]) // 4, 3 * len(rows[0]) // 4, 4)]
        if max(values) - min(values) < 6:
            fail("the water is one flat colour on a %s" % device)
    apart = mean_difference(pictures["computer"], pictures["phone"])
    if apart < 1.5:
        fail("the water looks the same on a computer and on a phone (%.2f apart): the phone version is not chosen" % apart)
    print("OK: water draws on a computer and on a phone, %.1f apart" % apart)
    if keep is not None:
        for device in ("computer", "phone"):
            shutil.copy2(work / ("water_%s.png" % device), keep / ("water_%s.png" % device))


LAYERS = ["#D9C28F", "#7A4A2A", "#2EA02E", "#146B3A", "#C8782A", "#7A7A86", "#E0E0F0", "#C020C0"]


def hex_rgb(text):
    return tuple(int(text[index:index + 2], 16) for index in (1, 3, 5))


def test_terrain(cli, project, work, keep):
    layers = ",".join("ground:%s:%s" % (color, color) for color in LAYERS)
    run(cli, "image", "array", "--project", project, "--name", "TestLayers", "--size", "64", "--layers", layers)
    scene = "TerrainTest"
    run(cli, "scene-new", "--project", project, "--name", scene, "--template", "Basic3D")
    run(cli, "entity", "remove", "--project", project, "--scene", scene, "--name", "Plane")
    run(cli, "entity", "set", "--project", project, "--scene", scene, "--name", "Main Camera", "--position", "0,70,-110", "--rotation", "24,0,0")
    run(cli, "terrain", "new", "--project", project, "--scene", scene, "--name", "Land", "--terrain", "/Game/Terrains/Land.kbterrain", "--preset", "mountains", "--resolution", "129", "--size", "128", "--seed", "3")
    info = run(cli, "terrain", "info", "--project", project, "--terrain", "/Game/Terrains/Land.kbterrain")
    heights = re.search(r"height[^\n]*?(-?\d+(?:\.\d+)?)\D+(-?\d+(?:\.\d+)?)", info, re.IGNORECASE)
    low, high = (float(heights.group(1)), float(heights.group(2))) if heights else (0.0, 40.0)
    if low > high:
        low, high = high, low
    run(cli, "material-new", "--project", project, "--preset", "terrain-layers", "--name", "Mountain", "--entity", "Land", "--scene", scene,
        "--set", "Layers=/Game/Images/TestLayers.ktx", "--set", "LayerCount=8",
        "--set", "Tiling=0.5", "--set", "SlopeStart=0.3")
    # The height range is read from the terrain, not typed: the instance carries its real lowest and highest ground.
    shown = run(cli, "material-instance-show", "--project", project, "--instance", "/Game/Materials/Mountain.kbmatinst")
    read = re.search(r"HeightMin = (-?[\d.]+).*?HeightMax = (-?[\d.]+)", shown, re.DOTALL)
    if not read or abs(float(read.group(1)) - low) > 0.05 or abs(float(read.group(2)) - high) > 0.05:
        fail("the terrain material did not take the heights of the terrain (%s..%s):%s%s" % (low, high, chr(10), shown))
    picture = work / "terrain.png"
    run(cli, "capture", "--project", project, "--scene", scene, "--camera", "Main Camera", "--out", picture, "--frames", "30", "--width", "640", "--height", "480")
    if keep is not None:
        shutil.copy2(picture, keep / "terrain.png")
    rows = read_png(picture)
    found = set()
    expected = [hex_rgb(color) for color in LAYERS]
    for y in range(0, len(rows), 2):
        for x in range(0, len(rows[0]), 2):
            pixel = rows[y][x]
            # The lit colour is darker or lighter than the texture: compare the hue direction, not the brightness.
            total = max(sum(pixel), 1)
            share = [channel / total for channel in pixel]
            best = min(range(len(expected)), key=lambda index: sum(abs(share[c] - expected[index][c] / max(sum(expected[index]), 1)) for c in range(3)))
            if sum(abs(share[c] - expected[best][c] / max(sum(expected[best]), 1)) for c in range(3)) < 0.06 and total > 60:
                found.add(best)
    if len(found) < 5:
        fail("only %d of the 8 terrain layers show in the picture (%s)" % (len(found), sorted(found)))
    print("OK: %d of the 8 layers of the array texture show on the terrain" % len(found))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=pathlib.Path, required=True)
    parser.add_argument("--keep", type=pathlib.Path)
    parser.add_argument("--only", choices=["outline", "wall", "water", "terrain"])
    options = parser.parse_args()
    cli, keep = options.cli.resolve(strict=True), options.keep
    if keep is not None:
        keep.mkdir(parents=True, exist_ok=True)
    work = pathlib.Path(tempfile.mkdtemp(prefix="kb_material_effects_"))
    try:
        run(cli, "new", "--parent", work, "--name", "Effects")
        project = work / "Effects"
        for name, test in (("outline", test_outline), ("wall", test_through_wall), ("water", test_water), ("terrain", test_terrain)):
            if options.only in (None, name):
                test(cli, project, work, keep)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()

"""Exercise kb_cli terrain: a terrain in a scene, brushes and paths, batches, material layers,
automatic painting, heightmap round trips, previews, and lossless rejection of wrong input."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

# Height hashes of the engine's own maths: a change in a brush or the file format shows here first.
GOLDEN_FLAT_129 = "3ed011c271f10e05"
GOLDEN_RAISE = "537d008342114f69"
GOLDEN_MOUNTAINS = "0e9cf1898b42dfc2"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--test-root", type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="TerrainAuthoring", dir=args.test_root)).resolve()
    project = root / "Sample"
    history = []

    def run(*tail, expected=0):
        result = subprocess.run([str(args.cli.resolve()), *map(str, tail)], capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=120)
        history.append(dict(arguments=list(map(str, tail)), exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout.strip() if expected == 0 else result.stderr.strip()

    def terrain(*tail, expected=0):
        return run("terrain", *tail, "--project", project, expected=expected)

    def value(text, key):
        found = re.search(rf"^{key} (.+)$", text, re.MULTILINE)
        assert found, (key, text)
        return found.group(1).strip()

    def height_hash(file):
        return value(terrain("info", "--terrain", file), "heightHash")

    def height(file, x, z):
        return float(value(terrain("sample", "--terrain", file, "--x", x, "--z", z), "height"))

    def weights(file, x, z):
        return [float(w) for w in value(terrain("sample", "--terrain", file, "--x", x, "--z", z), "weights").split()]

    def expect_hash(name, actual, golden):
        assert actual == golden, f"golden {name}: {actual}"

    run("new", "--parent", root, "--name", "Sample", "--template", "Empty")
    assets = project / "Assets"

    # A terrain in the scene: its file, an entity whose Mesh Renderer draws it.
    out = terrain("new", "--name", "Hills", "--size", "128", "--resolution", "129")
    hills = "/Game/Terrains/Hills.kbterrain"
    file = assets / "Terrains" / "Hills.kbterrain"
    assert file.is_file(), out
    shown = run("entity", "show", "--project", project, "--name", "Hills")
    assert "Hills.kbterrain" in shown, shown
    flat = height_hash(hills)
    expect_hash("flat", flat, GOLDEN_FLAT_129)
    info = terrain("info", "--terrain", hills)
    assert "samples 129x129" in info and "world size 128x128" in info and "layers 0" in info, info
    assert "x, z run from -64..64" in info, info

    # Rejected input leaves the file as it was.
    original = file.read_bytes()
    terrain("new", "--name", "Hills", "--size", "128", expected=1)             # entity and file exist
    terrain("new", "--name", "Bad", "--resolution", "100", expected=1)
    terrain("new", "--name", "Bad", "--resolution", "129", "--size", "-5", expected=1)
    assert not (assets / "Terrains" / "Bad.kbterrain").exists()
    for bad in (["--mode", "melt", "--x", "0", "--z", "0"], ["--x", "0"], ["--x", "500", "--z", "0"],
                ["--x", "0", "--z", "0", "--radius", "-3"], ["--x", "0", "--z", "0", "--radious", "3"],
                ["--x", "0", "--z", "0", "--strength", "0"], ["--path", "1,2;3"]):
        terrain("brush", "--terrain", hills, *bad, expected=1)
        assert file.read_bytes() == original, f"rejected brush changed the file: {bad}"
    terrain("sample", "--terrain", hills, "--x", "999", "--z", "0", expected=1)
    terrain("layer", "add", "--terrain", hills, "--material", "/Game/Materials/Nope.kbmat", expected=1)
    terrain("paint", "--terrain", hills, "--layer", "0", "--x", "0", "--z", "0", expected=1)  # no layers yet
    assert file.read_bytes() == original

    # A brush, and the same brush again on a second terrain gives the same heights.
    terrain("brush", "--terrain", hills, "--mode", "raise", "--x", "0", "--z", "0", "--radius", "16", "--strength", "3", "--falloff", "0.5")
    raised = height_hash(hills)
    assert raised != flat
    expect_hash("raise", raised, GOLDEN_RAISE)
    assert abs(height(hills, 0, 0) - 3.0) < 0.001
    assert abs(height(hills, 40, 40)) < 0.001
    terrain("new", "--terrain", assets / "Terrains" / "Copy.kbterrain", "--size", "128", "--resolution", "129")
    copy = assets / "Terrains" / "Copy.kbterrain"
    terrain("brush", "--terrain", copy, "--mode", "raise", "--x", "0", "--z", "0", "--radius", "16", "--strength", "3", "--falloff", "0.5")
    assert height_hash(copy) == raised, "the same stroke gave other heights"

    # A path, and a batch with the same stroke in the form of operations.
    terrain("brush", "--terrain", hills, "--mode", "raise", "--path", "-40,-30;40,-30;40,30", "--radius", "6", "--strength", "1.5", "--spacing", "2")
    assert height(hills, 0, -30) > 1.0 and height(hills, 40, 0) > 1.0
    ops = root / "ops.json"
    ops.write_text(json.dumps({"ops": [
        {"op": "brush", "mode": "flatten", "x": 0, "z": 0, "radius": 10, "height": 5, "strength": 1, "falloff": 0.5},
        {"op": "brush", "mode": "smooth", "path": [[-10, 0], [10, 0]], "radius": 6, "strength": 0.5},
        {"op": "brush", "mode": "cut-hole", "x": -50, "z": 50, "radius": 3}]}), encoding="utf-8")
    terrain("apply", "--terrain", hills, "--ops", ops)
    assert abs(height(hills, 0, 0) - 5.0) < 0.2
    assert "holes 0 cells" not in terrain("info", "--terrain", hills)
    before = file.read_bytes()
    terrain("apply", "--terrain", hills, "--ops", '[{"op":"brush","mode":"raise","x":0,"z":0},{"op":"nope"}]', expected=1)
    assert file.read_bytes() == before, "a failing batch changed the file"

    # Material layers, painted by hand and by slope.
    run("material-new", "--project", project, "--name", "Grass")
    run("material-new", "--project", project, "--name", "Rock")
    terrain("layer", "add", "--terrain", hills, "--material", "/Game/Materials/Grass.kbmat")
    terrain("layer", "add", "--terrain", hills, "--material", "/Game/Materials/Rock.kbmat")
    listing = terrain("layer", "list", "--terrain", hills)
    assert "0 /Game/Materials/Grass.kbmat" in listing and "1 /Game/Materials/Rock.kbmat" in listing, listing
    assert weights(hills, 0, 0)[:2] == [1.0, 0.0]
    terrain("paint", "--terrain", hills, "--layer", "1", "--path", "-50,-50;-20,-50", "--radius", "5")
    assert weights(hills, -35, -50)[1] > 0.99
    terrain("paint", "--terrain", hills, "--layer", "1", "--x", "-35", "--z", "-50", "--radius", "8", "--erase", "yes")
    assert weights(hills, -35, -50)[1] < 0.01
    terrain("brush", "--terrain", hills, "--mode", "raise", "--x", "0", "--z", "50", "--radius", "20", "--strength", "30", "--falloff", "0", "--shape", "soft-round")
    terrain("paint", "--terrain", hills, "--layer", "1", "--auto", "slope=45..90,height=-100..100")
    assert weights(hills, 10, 50)[1] > 0.9, weights(hills, 10, 50)
    assert weights(hills, -55, 0)[1] < 0.01
    terrain("paint", "--terrain", hills, "--layer", "1", "--auto", "slope=45..90", "--opacity", "7", expected=1)
    terrain("paint", "--terrain", hills, "--layer", "0", "--auto", "tilt=1..2", expected=1)
    terrain("layer", "set", "--terrain", hills, "--index", "1", "--material", "/Game/Materials/Grass.kbmat")
    terrain("layer", "remove", "--terrain", hills, "--index", "1")
    assert "layers 1" in terrain("info", "--terrain", hills)

    exports = project / "Exports"
    # Brushes beyond the plain ones: noise that joins up, ramp, terrace, stamp, symmetry, Ctrl, the eyedropper.
    sculpt = assets / "Terrains" / "Sculpt.kbterrain"
    terrain("new", "--terrain", sculpt, "--size", "128", "--resolution", "129")
    clean = sculpt.read_bytes()
    terrain("brush", "--terrain", sculpt, "--mode", "noise", "--x", "0", "--z", "0", "--radius", "40", "--strength", "4", "--falloff", "0.5",
            "--seed", "5", "--noise-scale", "20", "--symmetry", "xz")
    assert abs(height(sculpt, 12, 7) - height(sculpt, -12, -7)) < 0.002 and abs(height(sculpt, 12, 7) - height(sculpt, -12, 7)) < 0.002
    noisy = height_hash(sculpt)
    terrain("brush", "--terrain", sculpt, "--mode", "noise", "--x", "0", "--z", "0", "--radius", "40", "--strength", "4", "--falloff", "0.5",
            "--seed", "5", "--noise-scale", "20", "--symmetry", "xz", "--invert", "yes")    # Ctrl takes it away again
    assert abs(height(sculpt, 12, 7)) < 0.002 and height_hash(sculpt) != noisy
    sculpt.write_bytes(clean)
    terrain("brush", "--terrain", sculpt, "--mode", "raise", "--x", "40", "--z", "0", "--radius", "12", "--strength", "20", "--falloff", "1")
    terrain("brush", "--terrain", sculpt, "--mode", "ramp", "--x", "-40", "--z", "0", "--to", "40,0", "--radius", "6")  # heights: the terrain's own
    assert abs(height(sculpt, 0, 0) - 10.0) < 0.2, height(sculpt, 0, 0)
    terrain("brush", "--terrain", sculpt, "--mode", "ramp", "--x", "-40", "--z", "-30", "--to", "40,-30", "--radius", "4", "--height", "2", "--to-height", "12")
    assert abs(height(sculpt, 0, -30) - 7.0) < 0.1
    terrain("brush", "--terrain", sculpt, "--mode", "ramp", "--x", "-40", "--z", "-30", "--to", "40,-30", "--radius", "4", "--height", "2", expected=1)
    terrain("brush", "--terrain", sculpt, "--mode", "ramp", "--x", "0", "--z", "0", "--radius", "4", expected=1)
    terrain("brush", "--terrain", sculpt, "--mode", "terrace", "--x", "0", "--z", "-30", "--radius", "40", "--falloff", "1", "--step", "4", "--softness", "0.3")
    terrain("brush", "--terrain", sculpt, "--mode", "flatten", "--x", "40", "--z", "40", "--radius", "5", "--height", "sample", "--strength", "1", expected=1)  # flat already: nothing changes
    terrain("brush", "--terrain", sculpt, "--mode", "flatten", "--path", "40,0;40,40", "--radius", "5", "--height", "sample", "--falloff", "1")
    assert abs(height(sculpt, 40, 36) - height(sculpt, 40, 0)) < 0.5
    stamp_image = exports / "stamp.png"
    terrain("export", "--terrain", sculpt, "--out", stamp_image)
    terrain("brush", "--terrain", sculpt, "--mode", "stamp", "--image", stamp_image, "--x", "-40", "--z", "40", "--radius", "16", "--strength", "6",
            "--rotation", "45", "--blend", "add")
    assert height(sculpt, -40, 40) > 0.5
    terrain("brush", "--terrain", sculpt, "--mode", "stamp", "--x", "-40", "--z", "40", "--radius", "16", expected=1)               # no image
    terrain("brush", "--terrain", sculpt, "--mode", "stamp", "--image", exports / "missing.png", "--x", "0", "--z", "0", expected=1)
    terrain("brush", "--terrain", sculpt, "--mode", "raise", "--x", "0", "--z", "0", "--symmetry", "y", expected=1)
    terrain("brush", "--terrain", sculpt, "--mode", "noise", "--x", "0", "--z", "0", "--noise-octaves", "12", expected=1)
    terrain("apply", "--terrain", sculpt, "--ops", json.dumps([
        {"op": "brush", "mode": "stamp", "image": "Exports/stamp.png", "x": 20, "z": 50, "radius": 10, "strength": 3, "blend": "max"},
        {"op": "brush", "mode": "terrace", "x": 20, "z": 50, "radius": 12, "step": 2, "softness": 0.5}]))

    # Whole worlds: generators and erosion, repeatable on any number of threads, stoppable.
    world = assets / "Terrains" / "World.kbterrain"
    world_again = assets / "Terrains" / "WorldAgain.kbterrain"
    terrain("new", "--terrain", world, "--preset", "mountains", "--seed", "7", "--size", "512", "--resolution", "129")
    terrain("new", "--terrain", world_again, "--preset", "mountains", "--seed", "7", "--size", "512", "--resolution", "129", "--threads", "1")
    assert height_hash(world) == height_hash(world_again) != flat
    expect_hash("mountains preset", height_hash(world), GOLDEN_MOUNTAINS)
    terrain("new", "--terrain", assets / "Terrains" / "Bad.kbterrain", "--preset", "moon", expected=1)
    for preset in ("plains", "hills", "valley", "island", "plateau"):
        made = assets / "Terrains" / f"Preset_{preset}.kbterrain"
        terrain("new", "--terrain", made, "--preset", preset, "--seed", "3", "--resolution", "65", "--size", "256")
        assert "heightHash" in terrain("info", "--terrain", made)
    # The character of a world: the same seed, another character, another world; outside 0..1 is refused.
    mild = assets / "Terrains" / "Mild.kbterrain"
    dramatic = assets / "Terrains" / "Dramatic.kbterrain"
    terrain("new", "--terrain", mild, "--preset", "mountains", "--seed", "7", "--size", "512", "--resolution", "129", "--character", "0.1")
    terrain("new", "--terrain", dramatic, "--preset", "mountains", "--seed", "7", "--size", "512", "--resolution", "129", "--character", "0.9")
    assert len({height_hash(mild), height_hash(dramatic), height_hash(world)}) == 3
    terrain("new", "--terrain", assets / "Terrains" / "BadCharacter.kbterrain", "--preset", "mountains", "--character", "1.5", expected=1)
    drawn = terrain("info", "--terrain", world)
    assert float(value(drawn, "height").split()[3]) > 20, drawn
    terrain("generate", "--terrain", world, "--kind", "ridged", "--seed", "11", "--amplitude", "80", "--scale", "200", "--warp", "20")
    first = height_hash(world)
    terrain("generate", "--terrain", world_again, "--kind", "ridged", "--seed", "11", "--amplitude", "80", "--scale", "200", "--warp", "20", "--threads", "2")
    assert height_hash(world_again) == first
    terrain("generate", "--terrain", world, "--kind", "island", "--seed", "2", "--island-radius", "0.5", "--add", "yes")
    assert height_hash(world) != first
    terrain("generate", "--terrain", world, "--kind", "dunes", expected=1)
    terrain("generate", "--terrain", world, "--scale", "0", expected=1)
    terrain("generate", "--terrain", world, "--bogus", "1", expected=1)
    terrain("erode", "--terrain", world, "--mode", "hydraulic", "--iterations", "40", "--strength", "0.5")
    wet = height_hash(world)
    terrain("erode", "--terrain", world, "--mode", "thermal", "--iterations", "10", "--talus", "30")
    assert height_hash(world) != wet
    terrain("erode", "--terrain", world, "--mode", "thermal", "--rain", "0.1", expected=1)
    terrain("erode", "--terrain", world, "--mode", "hydraulic", "--iterations", "0", expected=1)
    before = world.read_bytes()
    big = assets / "Terrains" / "Big.kbterrain"
    terrain("new", "--terrain", big, "--resolution", "1025", "--size", "1024")
    big_bytes = big.read_bytes()
    stopped = terrain("erode", "--terrain", big, "--iterations", "100000", "--max-seconds", "0.5", expected=1)
    assert "time limit" in stopped and big.read_bytes() == big_bytes, stopped
    assert world.read_bytes() == before

    # Heightmaps out and back in, and previews.
    final = height_hash(hills)
    for extension in ("png", "raw"):
        image = exports / f"hills.{extension}"
        exported = terrain("export", "--terrain", hills, "--out", image)
        low, high = re.search(r"--min (\S+) --max (\S+)", exported).groups()
        back = assets / "Terrains" / f"Back_{extension}.kbterrain"
        terrain("import", "--terrain", back, "--source", image, "--min", low, "--max", high, "--size", "128")
        step = (float(high) - float(low)) / 65535.0
        for x, z in ((0, 0), (10, 50), (-40, 20), (63, -63)):
            assert abs(height(back, x, z) - height(hills, x, z)) <= 2 * step + 1e-4, (extension, x, z)
        terrain("import", "--terrain", back, "--source", image, expected=1)  # exists
        terrain("import", "--terrain", back, "--source", image, "--replace")
    for kind in ("height", "slope", "splat"):
        preview = exports / f"preview_{kind}.png"
        terrain("preview", "--terrain", hills, "--out", preview, "--kind", kind, "--pixels", "128")
        assert preview.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
    terrain("preview", "--terrain", hills, "--out", exports / "x.png", "--kind", "colour", expected=1)
    terrain("export", "--terrain", hills, "--out", exports / "x.jpg", expected=1)
    outside = root / "outside.png"
    terrain("preview", "--terrain", hills, "--out", outside, expected=1)  # a project command writes inside its project
    assert not outside.exists()
    assert height_hash(hills) == final, "read-only commands changed the terrain"

    # Rivers: a River in the scene (points added, moved, removed), a waterfall found, and the bed cut into a terrain.
    terrain("new", "--name", "Valley", "--size", "128", "--resolution", "129")
    valley = "/Game/Terrains/Valley.kbterrain"
    terrain("brush", "--terrain", valley, "--mode", "flatten", "--height", "8", "--x", "0", "--z", "0", "--radius", "100", "--falloff", "1")
    assert abs(height(valley, 0, 12) - 8.0) < 0.001
    out = terrain("river", "add", "--name", "Brook", "--points", "-50,6,0;0,6,12,6,1.5,2;50,4,0", "--bank-width", "4")
    assert "created river 'Brook'" in out and "3 in all" in out, out
    listing = terrain("river", "list", "--name", "Brook")
    assert "3 points" in listing and "1: 0,6,12 width 6 depth 1.5 speed 2" in listing and "0 waterfalls" in listing, listing
    assert "0: -50,6,0 width 4 depth 1 speed 1.5" in listing, listing   # the defaults of a point given by position only
    shown = run("entity", "show", "--project", project, "--name", "Brook")
    assert "River" in shown and "-50,6,0" in shown, shown
    terrain("river", "add", "--name", "Brook", "--points", "25,5,6", "--index", "2")
    assert "4 points" in terrain("river", "list", "--name", "Brook")
    terrain("river", "move", "--name", "Brook", "--index", "2", "--to", "25,5,3")
    moved = terrain("river", "list", "--name", "Brook")
    assert "2: 25,5,3 width 4 depth 1 speed 1.5" in moved, moved
    terrain("river", "move", "--name", "Brook", "--index", "1", "--to", "0,6,14")      # three numbers keep width, depth, speed
    assert "1: 0,6,14 width 6 depth 1.5 speed 2" in terrain("river", "list", "--name", "Brook")
    terrain("river", "remove", "--name", "Brook", "--index", "2")
    assert "3 points" in terrain("river", "list", "--name", "Brook")
    terrain("river", "move", "--name", "Brook", "--index", "1", "--to", "0,6,12")
    # Wrong input names the problem and changes nothing.
    scene_before = run("entity", "show", "--project", project, "--name", "Brook")
    terrain("river", "move", "--name", "Brook", "--index", "9", "--to", "0,0,0", expected=1)
    terrain("river", "move", "--name", "Brook", "--index", "1", "--to", "0,0", expected=1)
    terrain("river", "add", "--name", "Brook", "--points", "1,2,3,0,1,1", expected=1)              # no width
    terrain("river", "add", "--name", "Solo", "--points", "0,0,0", expected=1)                      # a new river needs two
    terrain("river", "add", "--name", "Brook", "--points", "0,0,0", "--bogus", "1", expected=1)
    terrain("river", "remove", "--name", "Brook", expected=1)
    terrain("river", "remove", "--name", "Hills", "--index", "0", expected=1)                       # no River on it
    terrain("river", "carve", "--name", "Brook", "--terrain", "/Game/Terrains/Nope.kbterrain", expected=1)
    assert run("entity", "show", "--project", project, "--name", "Brook") == scene_before
    # The bed: deepest in the middle (level 6, depth 1.5), the water edge at the level, the ground as it was beyond the bank.
    flat_valley = height_hash(valley)
    out = terrain("river", "carve", "--name", "Brook", "--terrain", valley)
    assert "carved river 'Brook'" in out and height_hash(valley) != flat_valley, out
    assert abs(height(valley, 0, 12) - 4.5) < 0.15, height(valley, 0, 12)
    assert abs(height(valley, 0, 15) - 6.0) < 0.15, height(valley, 0, 15)       # the edge of the water, 3 from the middle
    assert 6.0 < height(valley, 0, 17) < 8.0, height(valley, 0, 17)             # on the bank
    assert abs(height(valley, 0, 40) - 8.0) < 0.001 and abs(height(valley, -30, -30) - 8.0) < 0.001
    carved = height_hash(valley)
    terrain("river", "carve", "--name", "Nobody", "--terrain", valley, expected=1)
    assert height_hash(valley) == carved
    # A waterfall: a steep stretch between two points of the same river.
    terrain("river", "add", "--name", "Falls", "--points", "-30,12,-40;-10,12,-40;-6,4,-40;14,4,-40")
    falls = terrain("river", "list", "--name", "Falls")
    assert "1 waterfalls" in falls and "waterfall at" in falls and "drops 8" in falls, falls
    terrain("river", "add", "--name", "Falls", "--points", "0,0,0", "--waterfall-slope", "89", "--index", "0")
    assert "0 waterfalls" in terrain("river", "list", "--name", "Falls")
    terrain("river", "remove", "--name", "Falls", "--all")
    assert "1 river" in terrain("river", "list") and "Falls" not in terrain("river", "list")
    assert "river" in run("help", "terrain") and "carve" in run("help", "terrain")

    # The command is part of the agent surface.
    commands = run("commands")
    assert "terrain" in commands, commands
    assert "terrain" in run("help", "terrain") and "--auto" in run("help", "terrain")
    reply = json.loads(run("terrain", "info", "--terrain", hills, "--project", project, "--json"))
    assert "heightHash" in reply["output"], reply

    (root / "result.json").write_text(json.dumps(dict(result="PASS", commands=len(history))), encoding="utf-8")
    print(root)


if __name__ == "__main__":
    main()

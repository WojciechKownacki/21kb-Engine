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

    # Heightmaps out and back in, and previews.
    final = height_hash(hills)
    exports = project / "Exports"
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

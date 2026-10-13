#!/usr/bin/env python
"""Material domains from the command line to the picture (Unreal's Material Domain).

Post Process: a material of that domain on a Mesh Renderer (no mesh) is drawn over the whole view before the
tonemap. The project's game is captured headless (kb_game --headless --screenshot-file) before and after; with
Scene Color x 0.25 every pixel darkens, and a material writing Base Color in that domain is refused by name.

Usage: python scripts/check_material_domains.py [build/perf-release/bin]
"""
import pathlib
import shutil
import subprocess
import sys
import tempfile
import zlib
import struct


def fail(message):
    print("FAIL: " + message)
    sys.exit(1)


def run(tool, *args, expect=0):
    done = subprocess.run([str(tool), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if expect is not None and done.returncode != expect:
        fail("%s %s exited %d:\n%s%s" % (tool.name, " ".join(args), done.returncode, done.stdout, done.stderr))
    return done.stdout + done.stderr


def read_png(path):
    """RGBA8/RGB8 non-interlaced PNG to rows of (r, g, b)."""
    data = pathlib.Path(path).read_bytes()
    pos, width, height, channels, idat = 8, 0, 0, 0, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack(">IIBB", body[:10])
            channels = {2: 3, 6: 4}[color]
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    rows, previous = [], bytearray(stride)
    for y in range(height):
        kind = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left = line[x - channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x - channels] if x >= channels else 0
            if kind == 1:
                line[x] = (line[x] + left) & 255
            elif kind == 2:
                line[x] = (line[x] + up) & 255
            elif kind == 3:
                line[x] = (line[x] + (left + up) // 2) & 255
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[x] = (line[x] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 255
        rows.append([tuple(line[x * channels:x * channels + 3]) for x in range(width)])
        previous = line
    return rows


def capture(game, project, name):
    out = pathlib.Path(project).parent / (name + ".png")
    if out.exists():
        out.unlink()
    run(game, "--project=" + str(project), "--headless", "--frames=12", "--screenshot-frame=10", "--screenshot-file=" + str(out),
        "--width=320", "--height=180")
    return read_png(out)


def main():
    bin_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1] / "build" / "perf-release" / "bin")
    cli, game = bin_dir / "kb_cli.exe", bin_dir / "kb_game.exe"
    work = pathlib.Path(tempfile.mkdtemp(prefix="kb_domains_"))
    try:
        run(cli, "new", "--parent", str(work), "--name", "Domains")
        project = work / "Domains"
        baseline = capture(game, project, "scene")

        material = "/Game/Materials/Darken.kbmat"
        run(cli, "material-new", "--project", str(project), "--name", "Darken")
        graph = lambda *args, expect=0: run(cli, "material-graph", args[0], "--project", str(project), "--material", material, *args[1:], expect=expect)
        graph("setting", "--name", "Domain", "--value", "postProcess")
        graph("add-node", "--kind", "SceneColor", "--name", "Scene")
        graph("add-node", "--kind", "Constant", "--name", "Quarter", "--value", "0.25")
        graph("add-node", "--kind", "Multiply", "--name", "Darker")
        graph("connect", "--from", "Scene", "--to", "Darker", "--to-pin", "a")
        graph("connect", "--from", "Quarter", "--to", "Darker", "--to-pin", "b")
        graph("connect", "--from", "Darker", "--to", "Output", "--to-pin", "emissive")
        # Base Color in a Post Process material is refused by name (the graph keeps the link and does not compile).
        wrong = "/Game/Materials/Wrong.kbmat"
        run(cli, "material-new", "--project", str(project), "--name", "Wrong")
        run(cli, "material-graph", "setting", "--project", str(project), "--material", wrong, "--name", "Domain", "--value", "postProcess")
        run(cli, "material-graph", "add-node", "--project", str(project), "--material", wrong, "--kind", "Constant3Vector", "--name", "Red",
            "--value", "1 0 0")
        refused = run(cli, "material-graph", "connect", "--project", str(project), "--material", wrong, "--from", "Red", "--to", "Output",
                      "--to-pin", "baseColor")
        shown = run(cli, "material-graph", "show", "--project", str(project), "--material", wrong)
        if "MaterialDomainOutputRefused" not in refused or "Compiles: no" not in shown:
            fail("Base Color in a Post Process material must be refused by name, got:\n" + refused + shown)

        run(cli, "entity", "add", "--project", str(project), "--name", "Filter")
        run(cli, "component", "add", "--project", str(project), "--entity", "Filter", "--type", "MeshRenderer")
        run(cli, "component", "set", "--project", str(project), "--entity", "Filter", "--type", "MeshRenderer", "--set", "material=" + material)
        darkened = capture(game, project, "post_process")

        samples = [(x, y) for y in (20, 90, 160) for x in (20, 160, 300)]
        brighter = [(baseline[y][x], darkened[y][x]) for x, y in samples if sum(baseline[y][x]) > 60]
        if len(brighter) < 4:
            fail("the baseline scene is too dark to prove anything: %s" % [baseline[y][x] for x, y in samples])
        for before, after in brighter:
            if sum(after) >= sum(before) * 0.85:
                fail("Post Process Scene Color x 0.25 must darken every pixel: %s -> %s" % (before, after))
        print("OK: Post Process domain darkens the view (%s -> %s)" % (brighter[0][0], brighter[0][1]))

        # The cook: the game run from the cooked Game.kbpack draws the same darkened view. The project's refused
        # material is removed first (the cook stops on a graph that does not compile, by name).
        (project / "Assets" / "Materials" / "Wrong.kbmat").unlink()
        pack = work / "Game.kbpack"
        run(bin_dir / "kb_cooker.exe", "--project", str(project), "--target", "Windows.x64", "--output", str(pack))
        for plugin in ("physics_jolt", "audio_miniaudio", "basic_lighting", "21kb_particle"):
            shutil.copy2(bin_dir.parent / plugin / ("kb_%s_plugin.dll" % plugin), work)
        shot = work / "packaged.png"
        run(game, "--project=" + str(pack), "--headless", "--frames=12", "--screenshot-frame=10", "--screenshot-file=" + str(shot),
            "--width=320", "--height=180")
        packaged = read_png(shot)
        for x, y in samples:
            if any(abs(a - b) > 12 for a, b in zip(packaged[y][x], darkened[y][x])):
                fail("the packaged game must draw the Post Process material like the project: %s vs %s" % (packaged[y][x], darkened[y][x]))
        print("OK: the packaged game draws the Post Process material (%s)" % (packaged[samples[0][1]][samples[0][0]],))
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()

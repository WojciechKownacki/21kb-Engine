"""Compare shaped text with explicit Unicode reference forms in a cooked player."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "scripts"))
from png_readback import read_png


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("cli", "cooker", "player", "engine-root", "test-root", "shaderc"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", default=[])
    parser.add_argument("--runtime-dll", type=Path, action="append", default=[])
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="TextPackage", dir=args.test_root))
    project = root / "Sample"
    history = []

    def run(executable, *tail, cwd=root):
        command = list(map(str, (executable, *tail)))
        result = subprocess.run(command, cwd=cwd, capture_output=True, text=True, encoding="utf-8",
                                errors="replace", timeout=180,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        history.append(dict(command=command, code=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        require(result.returncode == 0, "Packaged text command failed: " + str(history[-1]))
        return result.stdout

    def component(entity, kind, *values):
        settings = [part for value in values for part in ("--set", value)]
        run(args.cli, "component", "add", "--project", project, "--entity", entity, "--type", kind, *settings)

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    font_root = args.engine_root / "sources/editor/content/EditorShell/Fonts"
    run(args.cli, "import", "--project", project, "--destination", "/Game/Fonts", font_root / "DejaVuSans.ttf")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "CanvasRoot")
    component("CanvasRoot", "Rect Transform", "anchorMax=1,1", "offsetMax=0,0")
    component("CanvasRoot", "Canvas")
    component("CanvasRoot", "Canvas Scaler", "scaleMode=0", "scaleFactor=1")
    component("CanvasRoot", "Border", "backgroundColor.r=0", "backgroundColor.g=0",
              "backgroundColor.b=0", "backgroundColor.a=1")
    # References use explicit presentation glyphs or directional overrides. They do not
    # derive the expected image from the engine's own shaping or cluster output.
    pairs = [
        ("arabic", "\u0633\u0644\u0627\u0645", "\ufeb3\ufefc\ufee1"),
        ("mixed", "abc \u05d0\u05d1 123", "\u202dabc 123 \u05d1\u05d0\u202c"),
        ("ligature", "ffi", "\ufb03"),
        ("accent", "a\u0301", "\u00e1"),
    ]
    for index, text in enumerate(value for _, actual, reference in pairs for value in (actual, reference)):
        name = "Line" + str(index)
        top = 10 + index * 70
        run(args.cli, "entity", "add", "--project", project, "--name", name, "--parent", "CanvasRoot")
        component("CanvasRoot/" + name, "Rect Transform", f"offsetMin=20,{top}", f"offsetMax=620,{top + 60}")
        component("CanvasRoot/" + name, "Text", "fontAssetId=/Game/Fonts/DejaVuSans.21kb",
                  "fontSize=48", "wrapMode=0", "content=" + text)

    output = root / "Packaged"
    output.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", output / "Game.kbpack",
        "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache")
    player = output / "Sample.exe"
    shutil.copy2(args.player, player)
    for source in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(source, output / source.name)
    shutil.copy2(font_root / "DejaVu-LICENSE.txt", output / "DejaVu-LICENSE.txt")
    # Make the original project unavailable before starting the package.
    project.rename(root / "SourceUnavailable")
    require(not (output / "Assets").exists(), "Loose assets must not supply the packaged font")
    picture = output / "text.png"
    text = run(player, "--headless", "--frames=8", "--uncapped", "--width=640", "--height=580",
               cwd=output)
    require("shutdown=clean" in text, "Packaged text runtime did not shut down cleanly")
    # The shipped player intentionally refuses development capture switches. Use the same
    # player binary outside the package directory for the instrumented pass, reading only
    # the immutable package; the shipped pass above verifies its normal startup separately.
    captured = run(args.player, "--project=" + str(output / "Game.kbpack"), "--headless", "--frames=8",
                   "--uncapped", "--width=640", "--height=580", "--renderer=d3d11",
                   "--screenshot-frame=6", "--screenshot-file=" + str(picture), cwd=output)
    require("shutdown=clean" in captured, "Instrumented package rendering did not shut down cleanly")
    rows = read_png(picture)
    require(len(rows) == 580 and len(rows[0]) == 640, "Unexpected capture dimensions")
    comparisons = []
    for index, (name, _, _) in enumerate(pairs):
        top = 10 + index * 140
        actual = [pixel for row in rows[top:top + 60] for pixel in row[20:620]]
        reference = [pixel for row in rows[top + 70:top + 130] for pixel in row[20:620]]
        ink = sum(max(pixel) > 80 for pixel in actual)
        difference = sum(max(abs(a - b) for a, b in zip(left, right)) > 2
                         for left, right in zip(actual, reference))
        require(ink > 50, name + " rendered no visible text")
        require(difference <= 4, f"{name}: shaped pixels differ from reference at {difference} pixels")
        comparisons.append(dict(case=name, visiblePixels=ink, differentPixels=difference))
    (root / "result.json").write_text(json.dumps(dict(result="PASS", comparisons=comparisons,
        sourceUnavailable=True, captureMode="development player reading immutable package",
        shippedPlayerPassed=True, screenshot=str(picture)), indent=2), encoding="utf-8")
    print("Packaged text shaping passed:", root)


if __name__ == "__main__":
    main()

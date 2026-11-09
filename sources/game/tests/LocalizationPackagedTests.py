"""Verify authored localized plurals and live locale changes in a cooked player."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import struct
import zlib

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
    root = Path(tempfile.mkdtemp(prefix="LocalizationPackage", dir=args.test_root))
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
    def asset_id(path, kind):
        value = 14695981039346656037
        for byte in (path + ":" + kind).encode():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        return value

    for name, rgb in [("Base", (255, 0, 0)), ("Localized", (0, 255, 0))]:
        def chunk(kind, payload):
            return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress((b"\0" + bytes(rgb) * 4) * 4)) + chunk(b"IEND", b"")
        source = root / (name + ".png")
        source.write_bytes(png)
        run(args.cli, "import", "--project", project, "--destination", "/Game/Images", source)
    base_image = asset_id("/Game/Images/Base.21kb", "ImportedAsset")
    localized_image = asset_id("/Game/Images/Localized.21kb", "ImportedAsset")
    for name, left, image in [("Mapped", 400, "Base")]:
        run(args.cli, "entity", "add", "--project", project, "--name", name, "--parent", "CanvasRoot")
        component("CanvasRoot/" + name, "Rect Transform", f"offsetMin={left},20", f"offsetMax={left+40},60")
        component("CanvasRoot/" + name, "Raw Image", f"imageAssetId=/Game/Images/{image}.21kb")
    catalog_dir = root / "CatalogSources"
    catalog_dir.mkdir()
    # Select a stable ID above INT64_MAX to exercise the full-width Lua string path.
    for suffix in range(100):
        catalog_name = f"Labels{suffix}"
        catalog_id = 14695981039346656037
        for byte in f"/Game/Localization/{catalog_name}.kbloc:LocalizationCatalog".encode():
            catalog_id = ((catalog_id ^ byte) * 1099511628211) & ((1 << 64) - 1)
        if catalog_id > (1 << 63):
            break
    require(catalog_id > (1 << 63), "Fixture must cover a full-width catalog ID")
    (catalog_dir / (catalog_name + '.kbloc')).write_text(f'''schema 2
fallback "EN"
language "en"
plural "count.fallback" "one" "{{count}} item"
plural "count.fallback" "other" "{{count}} items"
plural "count.local" "one" "{{count}} item"
plural "count.local" "other" "{{count}} items"
language "pl"
resource "{base_image:016x}" "{localized_image:016x}"
plural "count.local" "one" "{{count}} element"
plural "count.local" "few" "{{count}} elementy"
plural "count.local" "many" "{{count}} elementow"
plural "count.local" "other" "{{count}} elementu"
''', encoding='utf-8')
    run(args.cli, "import", "--project", project, "--destination", "/Game/Localization", catalog_dir / (catalog_name + '.kbloc'))
    pairs = [
        ("integer", "count.fallback", "1", "1 item", "1 item"),
        ("decimal", "count.fallback", "1.00", "1.00 items", "1.00 items"),
        ("regional", "count.local", "2", "2 items", "2 elementy"),
        ("missing", "missing.key", "2", "missing.key", "missing.key"),
    ]
    for index, (_, key, number, english, polish) in enumerate(pairs):
        for lane, text in enumerate(("authored", english, polish)):
            name = f"Line{index}_{lane}"
            top = 10 + (index * 3 + lane) * 70
            run(args.cli, "entity", "add", "--project", project, "--name", name, "--parent", "CanvasRoot")
            component("CanvasRoot/" + name, "Rect Transform", f"offsetMin=20,{top}", f"offsetMax=620,{top + 60}")
            values = ["fontAssetId=/Game/Fonts/DejaVuSans.21kb", "fontSize=40", "wrapMode=0", "content=" + text]
            if lane == 0:
                values += ["localizationKey=" + key, "localizationNumber=" + number]
            component("CanvasRoot/" + name, "Text", *values)
    run(args.cli, "script-new", "--project", project, "--name", "LocaleFlow", "--language", "lua")
    (project / "Assets/Scripts/LocaleFlow.lua").write_text(f'''-- @asset /Game/Localization/{catalog_name}.kbloc
local ticks = 0
function Ready(self, dt)
    assert(not pcall(Localization.SetCatalog, "invalid-id"))
    assert(Localization.SetCatalog("{catalog_id:016x}"))
    assert(Localization.SetLanguage("eN_Us"))
    assert(not Localization.SetLanguage("en--US"))
    assert(Localization.ResolveResource("{base_image:016x}") == "{base_image:016x}")
    Log("LocaleReady")
end
function Tick(self, dt)
    ticks = ticks + 1
    if ticks == 4 then
        assert(Localization.SetLanguage("PL_pl"))
        assert(Localization.FormatPluralNumber("count.fallback", "1.00") == "1.00 items")
        assert(Localization.ResolveResource("{base_image:016x}") == "{localized_image:016x}")
        Log("LocaleChanged")
    end
end
''', encoding="utf-8")
    run(args.cli, "scene-attach", "--project", project, "--scene", "Assets/Scenes/Main.21kbscene",
        "--node", "CanvasRoot", "--script", "/Game/Scripts/LocaleFlow.lua")

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
    text = run(player, "--headless", "--frames=8", "--uncapped", "--width=640", "--height=860", cwd=output)
    require("shutdown=clean" in text and "script: LocaleReady" in text and "script: LocaleChanged" in text,
            "Shipped localized runtime failed")
    comparisons = []
    for locale, frame, reference_lane in [("en", 2, 1), ("pl", 6, 2)]:
        picture = output / (locale + ".png")
        captured = run(args.player, "--project=" + str(output / "Game.kbpack"), "--headless", "--frames=8",
            "--uncapped", "--width=640", "--height=860", "--renderer=d3d11",
            "--screenshot-frame=" + str(frame), "--screenshot-file=" + str(picture), cwd=output)
        require("shutdown=clean" in captured and "script: LocaleChanged" in captured, "Locale capture failed")
        rows = read_png(picture)
        require(len(rows) == 860 and len(rows[0]) == 640, "Unexpected capture dimensions")
        expected = (255, 0, 0) if locale == "en" else (0, 255, 0)
        observed = rows[40][420]
        require(all(abs(a-b) < 8 for a,b in zip(observed, expected)), f"Localized resource did not render: {locale}/{observed}")
        for index, (name, _, _, _, _) in enumerate(pairs):
            top = 10 + index * 210
            actual = [pixel for row in rows[top:top + 60] for pixel in row[20:380]]
            ref_top = top + reference_lane * 70
            reference = [pixel for row in rows[ref_top:ref_top + 60] for pixel in row[20:380]]
            ink = sum(max(pixel) > 80 for pixel in actual)
            difference = sum(max(abs(a-b) for a,b in zip(left,right)) > 2 for left,right in zip(actual,reference))
            require(ink > 50 and difference <= 4, f"{locale}/{name}: invalid localized pixels ({ink}, {difference})")
            comparisons.append(dict(locale=locale, case=name, visiblePixels=ink, differentPixels=difference))
    (root / "result.json").write_text(json.dumps(dict(result="PASS", comparisons=comparisons,
        sourceUnavailable=True, shippedPlayerPassed=True, fullWidthCatalogId=f"{catalog_id:016x}"), indent=2), encoding="utf-8")
    print("Packaged localization passed:", root)



if __name__ == "__main__":
    main()

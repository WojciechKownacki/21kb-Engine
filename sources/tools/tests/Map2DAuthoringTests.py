"""Exercise palette image import, painting and lossless rejection of invalid map sizes."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ["cli", "image", "test-root"]:
        parser.add_argument("--"+option, type=Path, required=True)
    args = parser.parse_args()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="MapAuthoring", dir=args.test_root)).resolve()
    project = root / "Sample"
    history = []
    def run(*tail, expected=0):
        result = subprocess.run([str(args.cli.resolve()), *map(str, tail)], capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=60)
        history.append(dict(arguments=list(map(str,tail)), exitCode=result.returncode,
                            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout.strip()
    run("new", "--parent", root, "--name", "Sample", "--template", "Empty")
    def map_command(*tail, expected=0):
        return run("map2d", *tail, "--project", project, expected=expected)
    texture = map_command("import-image", "--source", args.image.resolve())
    duplicate = map_command("import-image", "--source", args.image.resolve())
    assert texture != duplicate
    imported = project / "Assets" / texture.removeprefix("/Game/")
    assert imported.read_bytes() == args.image.read_bytes()
    map_command("new", "--name", "Map", "--size", "4x3")
    file = next((project / "Assets").rglob("*.kbmap2d"))
    virtual = "/Game/" + file.relative_to(project / "Assets").as_posix()
    map_command("ground", "new", "--name", "Floor", "--texture", texture, "--tiles", "0")
    map_command("fill", "--map", virtual, "--ground", "Floor", "--at", "0,0")
    map_command("erase", "--map", virtual, "--at", "0,0")
    map_command("paint", "--map", virtual, "--at", "3,2", "--sprite", texture, "--cell", "0", "--cell-size", "32", "--phase", "0.25")
    assert "/0.25" in file.read_text(encoding="utf-8")
    original = file.read_bytes()
    for size in ["4097x3", "0x3", "4294967295x3"]:
        map_command("set", "--map", virtual, "--size", size, expected=1)
        assert file.read_bytes() == original, "Rejected resize changed map data"
    for action in ["paint", "erase"]:
        for spot in ["4,2", "-1,0", "4294967296,0", "9223372036854775807,0"]:
            extra = ["--sprite", texture, "--cell", "0"] if action == "paint" else []
            map_command(action, "--map", virtual, "--at", spot, *extra, expected=1)
            assert file.read_bytes() == original, "Out-of-range edit changed map data"
    info = map_command("info", "--map", virtual)
    assert "chunks 16x16" in info and "1..4096" in info
    map_command("set", "--map", virtual, "--size", "4x3")
    assert "/0.25" in file.read_text(encoding="utf-8"), "CLI reload discarded phase"
    (root / "result.json").write_text(json.dumps(dict(result="PASS",commands=len(history),rejectedSizes=3,
        rejectedCoordinates=8,phaseReload=True,fileUnchanged=True,paletteImport=True,existingImagePreserved=True)),encoding="utf-8")
    print(root)


if __name__ == "__main__":
    main()

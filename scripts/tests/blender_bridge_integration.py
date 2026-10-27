"""Exercise saved Blender edits, live reimport and material conflicts with real tools."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--blender", required=True)
    parser.add_argument("--test-root", required=True, type=Path)
    args = parser.parse_args()
    args.cli = args.cli.resolve()
    args.blender = Path(shutil.which(args.blender) or args.blender).resolve()
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="BlenderBridge", dir=args.test_root)).resolve()
    project, source = root / "Sample", root / "Model.blend"
    bridge = Path(__file__).resolve().parents[1] / "blender_bridge.py"
    history = []

    def run(command, expected=0):
        result = subprocess.run(list(map(str, command)), capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=120)
        history.append(dict(command=list(map(str, command)), code=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout

    def save_source(scale, roughness):
        script = root / "fixture.py"
        script.write_text(f'''import bpy
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_cube_add(size=2)
obj = bpy.context.object
obj.name = "SampleMesh"
for vertex in obj.data.vertices:
    vertex.co.x *= {scale}
mat = bpy.data.materials.new("Surface")
mat.use_nodes = True
mat.node_tree.nodes.get("Principled BSDF").inputs["Roughness"].default_value = {roughness}
obj.data.materials.append(mat)
bpy.ops.wm.save_as_mainfile(filepath={str(source)!r})
''', encoding="utf-8")
        run([args.blender, "--background", "--factory-startup", "--disable-autoexec", "--threads", "2",
            "--python-exit-code", "1", "--python", script])

    run([args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty"])
    save_source(1, 0.25)
    command = [sys.executable, "-X", "utf8", bridge, "--project", project, "--source", source,
        "--cli", args.cli, "--blender", args.blender, "--poll-seconds", "0.1"]
    missing = command.copy()
    missing[missing.index("--blender") + 1] = root / "MissingBlender.exe"
    assert "Blender was not found" in run(missing, expected=1)
    cancel = root / "cancel"
    watch_log = root / "watch.jsonl"
    with watch_log.open("w", encoding="utf-8") as output:
        watcher = subprocess.Popen(list(map(str, [*command, "--watch", "--cancel-file", cancel])),
            stdout=output, stderr=subprocess.STDOUT)
        try:
            def wait_imports(count):
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline:
                    lines = watch_log.read_text(encoding="utf-8").splitlines()
                    rows = [json.loads(line) for line in lines if line.startswith("{")]
                    assert not any(row["status"] == "failed" for row in rows), rows
                    if sum(row["status"] == "imported" for row in rows) >= count:
                        return rows
                    assert watcher.poll() is None, lines
                    time.sleep(0.1)
                raise AssertionError("Watcher did not import the saved scene: " + watch_log.read_text())

            wait_imports(1)
            mesh = project / "Assets/Models/Model.21kb"
            assert mesh.is_file(), list((project / "Assets/Models").rglob("*"))
            first_mesh = digest(mesh)
            materials = list((project / "Assets/Models").rglob("*.kbmat"))
            assert len(materials) == 1, materials
            material = materials[0]
            save_source(2, 0.5)
            rows = wait_imports(2)
            assert digest(mesh) != first_mesh, "Saved Blender geometry did not change the imported engine mesh"
            cancel.touch()
            assert watcher.wait(timeout=10) == 0
        finally:
            if watcher.poll() is None:
                watcher.kill()
                watcher.wait()
    assert '"status": "cancelled"' in watch_log.read_text(encoding="utf-8")
    edited, replacements = re.subn(r"roughnessFactor [^\r\n]+", "roughnessFactor 0.875",
        material.read_text(encoding="utf-8"))
    assert replacements == 1, edited
    material.write_text(edited, encoding="utf-8")
    local_material = material.read_bytes()
    mesh_before_conflict = mesh.read_bytes()
    save_source(3, 0.1)
    assert '"status": "failed"' in run(command, expected=1)
    assert material.read_bytes() == local_material and mesh.read_bytes() == mesh_before_conflict
    assert '"status": "imported"' in run([*command, "--on-conflict", "keep-local"])
    assert material.read_bytes() == local_material, "Reimport overwrote the local material override"
    assert mesh.read_bytes() != mesh_before_conflict, "Keep-local must still update changed geometry"
    run([args.cli, "entity", "add", "--project", project, "--name", "ImportedModel"])
    run([args.cli, "component", "add", "--project", project, "--entity", "ImportedModel", "--type", "MeshRenderer",
        "--set", "mesh=/Game/Models/Model.21kb"])
    run([args.cli, "run", "--project", project, "--frames", "2"])
    report = dict(result="PASS", watchedUpdates=2, geometryChanged=True, conflictPreservedFiles=True,
        keepLocalPreservedMaterial=True, cancelFileStoppedWatcher=True, missingBlenderDiagnostic=True, engineSceneRan=True,
        sourceHashes=[row["sourceHash"] for row in rows if row["status"] == "imported"])
    (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(**report, artifacts=str(root))))


if __name__ == "__main__":
    main()

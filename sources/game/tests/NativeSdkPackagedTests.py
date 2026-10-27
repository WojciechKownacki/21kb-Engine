"""Build an external gameplay module through CLI and run its cooked Windows package."""
import argparse
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ["cli", "cooker", "player", "engine-root", "shaderc", "test-root"]:
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--plugin", type=Path, action="append", required=True)
    parser.add_argument("--runtime-dll", type=Path, action="append", default=[])
    args = parser.parse_args()
    assert os.name == "nt", "This package test requires the Windows host"
    ctypes.windll.kernel32.SetErrorMode(1 | 2 | 0x8000)
    args.test_root.mkdir(parents=True, exist_ok=True)
    root = Path(tempfile.mkdtemp(prefix="NativeSdk", dir=args.test_root))
    project, package = root / "Sample", root / "Package"
    history = []

    def run(executable, *tail, expected=0, cwd=root):
        result = subprocess.run([str(executable), *map(str, tail)], cwd=cwd,
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180,
            creationflags=subprocess.CREATE_NO_WINDOW)
        history.append(dict(command=list(map(str, [executable, *tail])), exitCode=result.returncode,
            stdout=result.stdout, stderr=result.stderr))
        (root / "commands.json").write_text(json.dumps(history, indent=2), encoding="utf-8")
        assert result.returncode == expected, history[-1]
        return result.stdout + result.stderr

    run(args.cli, "new", "--parent", root, "--name", "Sample", "--template", "Empty")
    run(args.cli, "entity", "add", "--project", project, "--create", "Camera", "--name", "MainCamera")
    run(args.cli, "entity", "add", "--project", project, "--name", "NativeHost")
    run(args.cli, "script-new", "--project", project, "--name", "NativeProbe", "--language", "cpp")
    # An unavailable real compiler exercises CMake's toolchain diagnostics through
    # the generated CLI build command, without removing any installed compiler.
    cmake = project / "Source/NativeScripts/NativeProbe/CMakeLists.txt"
    original_cmake = cmake.read_text(encoding="utf-8")
    cmake.write_text(original_cmake.replace("project(NativeProbe LANGUAGES CXX)",
        'set(CMAKE_CXX_COMPILER "kb_missing_cpp_compiler" CACHE FILEPATH "" FORCE)\n'
        'project(NativeProbe LANGUAGES CXX)'), encoding="utf-8")
    diagnostic = run(args.cli, "script-build", "--project", project,
        "--script", "/Game/Scripts/NativeProbe.native", expected=1)
    assert "kb_missing_cpp_compiler" in diagnostic and "install the C++ compiler, CMake" in diagnostic
    assert "retry script-build" in diagnostic
    cmake.write_text(original_cmake, encoding="utf-8")
    run(args.cli, "script-build", "--project", project, "--script", "/Game/Scripts/NativeProbe.native")
    run(args.cli, "scene-attach", "--project", project, "--scene", "/Game/Scenes/Main.21kbscene",
        "--node", "NativeHost", "--script", "/Game/Scripts/NativeProbe.native")
    output = run(args.cli, "run", "--project", project, "--frames", "3")
    assert "NativeProbe ready" in output and "0 diagnostics" in output
    source = project / "Source/NativeScripts/NativeProbe/NativeProbe.cpp"
    current_source = source.read_text(encoding="utf-8")
    guard = "api->version == kb::script::kNativeScriptPluginApiVersion"
    assert guard in current_source, "Generated modules must check the SDK ABI before registering"
    # Compile a real DLL that accepts only the former registration API version.
    # This is a compatibility fixture, not a claim to build an entire old SDK.
    source.write_text(current_source.replace(guard, "api->version == 1U"), encoding="utf-8")
    run(args.cli, "script-build", "--project", project, "--script", "/Game/Scripts/NativeProbe.native")
    refused = run(args.cli, "run", "--project", project, "--frames", "3", expected=1)
    assert "SDK API " in refused and "rebuild the plugin against the current SDK" in refused
    assert "NativeProbe ready" not in refused
    source.write_text(current_source, encoding="utf-8")
    package.mkdir()
    run(args.cooker, "--project", project, "--target", "Windows.x64", "--output", package / "Game.kbpack",
        "--engine-root", args.engine_root, "--shaderc", args.shaderc, "--cache", root / "CookCache",
        "--runtime-modules-output", package / "RuntimeModules")
    module = package / "RuntimeModules/Binaries/NativeScripts/NativeProbe.dll"
    assert module.is_file(), "Cook must stage the external module"
    shutil.copy2(args.player, package / "Sample.exe")
    for dependency in [*args.plugin, *args.runtime_dll]:
        shutil.copy2(dependency, package / dependency.name)
    # Removing the original location proves the player uses packaged data only.
    project.rename(root / "SourceUnavailable")
    output = run(package / "Sample.exe", "--headless", "--frames=3", "--width=64", "--height=64", cwd=package)
    assert "NativeProbe ready" in output and "rendered=3" in output and "shutdown=clean" in output
    assert not (package / "Source").exists() and not (package / "Assets").exists()
    report = dict(result="PASS", packagedFrames=3, externalModule=True,
        missingCompilerDiagnostic=True, compiledAbiMismatch=True, sourceUnavailable=True)
    (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(dict(**report, artifacts=str(root))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

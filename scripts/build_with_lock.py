"""Build one CMake target, or run one packaging command, inside the Visual Studio Build Tools x64 environment.

  python scripts/build_with_lock.py --preset perf-release --target kb_game
  python scripts/build_with_lock.py --run scripts/package_game.py --project ...
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_TOOLS = Path(os.environ.get("KB_BUILD_TOOLS", r"C:\BuildTools"))


def msvc_environment() -> dict[str, str]:
    environment = dict(os.environ)
    vcvars = BUILD_TOOLS / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if os.name == "nt" and vcvars.is_file():
        dump = subprocess.run(f'cmd /s /c ""{vcvars}" >nul && set"', capture_output=True, text=True, shell=True)
        for line in dump.stdout.splitlines():
            key, separator, value = line.partition("=")
            if separator and key:
                environment[key] = value
    cmake = BUILD_TOOLS / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake"
    environment["PATH"] = os.pathsep.join([str(cmake / "CMake" / "bin"), str(cmake / "Ninja"), environment.get("PATH", "")])
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--preset", help="build directory name under build/")
    parser.add_argument("--target", help="single CMake target; clean, rebuild and all are refused")
    parser.add_argument("--run", nargs=argparse.REMAINDER, help="python script under scripts/ and its arguments")
    args = parser.parse_args()
    environment = msvc_environment()
    if args.run:
        script = (ROOT / args.run[0]).resolve()
        if script.parent != ROOT / "scripts":
            parser.error("--run accepts only scripts/ files")
        return subprocess.call([sys.executable, str(script), *args.run[1:]], cwd=ROOT, env=environment)
    if not args.preset or not args.target or args.target in ("all", "clean", "rebuild", "ALL_BUILD"):
        parser.error("--preset and a single --target are required")
    cmake = shutil.which("cmake", path=environment["PATH"]) or "cmake"
    return subprocess.call([cmake, "--build", str(ROOT / "build" / args.preset), "--target", args.target],
                           cwd=ROOT, env=environment)


if __name__ == "__main__":
    raise SystemExit(main())

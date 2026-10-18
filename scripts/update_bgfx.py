#!/usr/bin/env python3
"""Replace third_party/bgfx.cmake with upstream bgfx.cmake, bgfx, bx and bimg, then apply our patches.

third_party/bgfx.cmake is always upstream plus third_party/bgfx-patches/*.patch, nothing else:
a change to bgfx is a new patch there, never an edit that only lives in the tree.

    python scripts/update_bgfx.py                   # newest master of all four
    python scripts/update_bgfx.py --bgfx <full commit hash>   # pin one of them

A patch that no longer applies is merged three-way and left with conflict markers; fix it, then
rewrite that patch file from the resolved tree.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ENGINE_ROOT = Path(__file__).resolve().parent.parent
TREE = Path("third_party/bgfx.cmake")
PATCHES = Path("third_party/bgfx-patches")
MANIFEST = Path("third_party/third_party_manifest.json")
LICENSES = Path("third_party/THIRD_PARTY_LICENSES.md")
REPOSITORIES = {
    "bgfx.cmake": "https://github.com/bkaradzic/bgfx.cmake.git",
    "bgfx": "https://github.com/bkaradzic/bgfx.git",
    "bx": "https://github.com/bkaradzic/bx.git",
    "bimg": "https://github.com/bkaradzic/bimg.git",
}


class UpdateError(RuntimeError):
    pass


def git(*arguments: str, cwd: Path, check: bool = True) -> str:
    result = subprocess.run(["git", *arguments], cwd=cwd, capture_output=True, text=True, encoding="utf-8")
    if check and result.returncode != 0:
        raise UpdateError(f"git {' '.join(arguments)} failed:\n{result.stderr.strip()}")
    return result.stdout.strip()


def fetch(name: str, ref: str, destination: Path) -> str:
    destination.mkdir(parents=True, exist_ok=True)
    git("init", "-q", cwd=destination)
    git("fetch", "-q", "--depth", "1", REPOSITORIES[name], ref, cwd=destination)
    git("checkout", "-q", "FETCH_HEAD", cwd=destination)
    return git("rev-parse", "--short=7", "HEAD", cwd=destination)


def replace_tree(source: Path, target: Path) -> None:
    if target.exists():
        shutil.rmtree(target)
    shutil.copytree(source, target, ignore=shutil.ignore_patterns(".git", ".gitmodules"))


def apply_patches(engine_root: Path) -> list[str]:
    conflicts: list[str] = []
    for patch in sorted((engine_root / PATCHES).glob("*.patch")):
        command = ["git", "apply", "--whitespace=nowarn", f"--directory={TREE.as_posix()}", str(patch)]
        # The files as upstream wrote them first (some keep CRLF); a three-way merge, which works
        # on the index, only for a patch whose context upstream has moved.
        result = subprocess.run(command, cwd=engine_root, capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            git("add", "-A", "--", TREE.as_posix(), cwd=engine_root)
            result = subprocess.run(command[:2] + ["--3way"] + command[2:],
                                    cwd=engine_root, capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            conflicts.append(f"{patch.name}:\n{result.stderr.strip()}")
        print(f"{'conflict' if result.returncode else 'applied '} {patch.name}")
    return conflicts


def record_versions(engine_root: Path, versions: dict[str, str]) -> None:
    # Only the version values change: the manifest keeps one hand-formatted line per component.
    manifest_path = engine_root / MANIFEST
    text = manifest_path.read_bytes().decode("utf-8")
    for name in ("bgfx", "bx", "bimg"):
        text, count = re.subn(rf'(\{{ "id": "{name}", "name": "[^"]+", "version": )"[^"]*"', rf'\g<1>"{versions[name]}"', text)
        if count != 1:
            raise UpdateError(f"{MANIFEST} has no single entry for {name}")
    json.loads(text)
    manifest_path.write_bytes(text.encode("utf-8"))
    licenses_path = engine_root / LICENSES
    text = licenses_path.read_bytes().decode("utf-8")
    text = re.sub(r"(## bgfx\r?\n(?:.*\n)*?- Revision: )`[0-9a-f]+`", rf"\g<1>`{versions['bgfx']}`", text, count=1)
    licenses_path.write_bytes(text.encode("utf-8"))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    for name in REPOSITORIES:
        parser.add_argument(f"--{name.replace('.', '-')}", default="master", metavar="REF",
                            help=f"{name} branch, tag or full commit hash (default: master)")
    args = parser.parse_args(argv)
    refs = {name: getattr(args, name.replace(".", "_").replace("-", "_")) for name in REPOSITORIES}

    try:
        with tempfile.TemporaryDirectory(prefix="kb_bgfx_") as temporary:
            work = Path(temporary) / "bgfx.cmake"
            versions = {"bgfx.cmake": fetch("bgfx.cmake", refs["bgfx.cmake"], work)}
            for name in ("bgfx", "bx", "bimg"):
                checkout = work / name
                if checkout.exists():
                    shutil.rmtree(checkout)
                versions[name] = fetch(name, refs[name], checkout)
            print("upstream: " + ", ".join(f"{name} {commit}" for name, commit in versions.items()))
            replace_tree(work, ENGINE_ROOT / TREE)
        conflicts = apply_patches(ENGINE_ROOT)
        record_versions(ENGINE_ROOT, versions)
    except UpdateError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    if conflicts:
        print("\nPatches that need a manual merge:\n\n" + "\n\n".join(conflicts), file=sys.stderr)
        return 2
    print("\nNext: regenerate the prebuilt renderer shaders and build and test the engine.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

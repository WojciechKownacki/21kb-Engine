#!/usr/bin/env python3
"""Runs the clang-tidy checks that .clang-tidy lists under WarningsAsErrors over every first-party source.

The broader check set in .clang-tidy stays advisory for editors and IDEs; the WarningsAsErrors subset is
the gate: any finding fails the run. Needs a compile_commands.json (configure with
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Sequence

ENGINE_ROOT = Path(__file__).resolve().parents[1]
FIRST_PARTY = re.compile(r"(^|[\\/])sources[\\/]")
EXCLUDED = re.compile(r"[\\/](third_party|_deps|build)[\\/]")


def gate_checks(config: Path) -> list[str]:
    text = config.read_text(encoding="utf-8")
    match = re.search(r"^WarningsAsErrors:\s*(?:>\s*\n((?:[ \t]+.*\n?)+)|'([^']*)')", text, re.MULTILINE)
    if match is None:
        raise SystemExit(f"{config}: no WarningsAsErrors list")
    listed = match.group(1) if match.group(1) is not None else match.group(2)
    checks = [check.strip() for check in listed.replace("\n", ",").split(",") if check.strip()]
    if not checks:
        raise SystemExit(f"{config}: WarningsAsErrors is empty")
    return checks


def first_party_sources(build_dir: Path) -> list[str]:
    entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    files = {
        str(Path(entry["directory"], entry["file"]).resolve())
        for entry in entries
        if FIRST_PARTY.search(entry["file"]) and not EXCLUDED.search(entry["file"])
    }
    return sorted(files)


def find_clang_tidy(explicit: str | None) -> str:
    candidates = [explicit] if explicit else []
    candidates += [shutil.which("clang-tidy") or ""]
    candidates += [str(Path(os.environ.get("VCINSTALLDIR", "")) / "Tools/Llvm/x64/bin/clang-tidy.exe")]
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise SystemExit("clang-tidy was not found; pass --clang-tidy")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--clang-tidy")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    parser.add_argument("--filter", default="", help="only sources whose path matches this regular expression")
    parser.add_argument("--filter-file", type=Path, help="read the --filter expression from this file")
    parser.add_argument(
        "--changed-since",
        help="only sources changed since this git revision; a changed first-party header checks every source",
    )
    args = parser.parse_args(argv)

    checks = gate_checks(ENGINE_ROOT / ".clang-tidy")
    tidy = find_clang_tidy(args.clang_tidy)
    pattern = args.filter_file.read_text(encoding="utf-8").strip() if args.filter_file else args.filter
    sources = [path for path in first_party_sources(args.build_dir) if re.search(pattern, path)]
    if args.changed_since:
        changed = subprocess.run(
            ["git", "diff", "--name-only", f"{args.changed_since}...HEAD"],
            cwd=ENGINE_ROOT, capture_output=True, text=True, check=True,
        ).stdout.split()
        changed_paths = {str((ENGINE_ROOT / name).resolve()).lower() for name in changed}
        header_changed = any(
            FIRST_PARTY.search(name) and Path(name).suffix in (".h", ".hpp", ".inl") for name in changed
        )
        if not header_changed:
            sources = [path for path in sources if path.lower() in changed_paths]
    command = [
        tidy, "-p", str(args.build_dir), "--quiet", f"--checks=-*,{','.join(checks)}", "--warnings-as-errors=*",
        # MSVC-only switches in the compile database mean nothing to the clang front end.
        "--extra-arg=-Wno-unused-command-line-argument", "--extra-arg=-Wno-unknown-argument",
    ]

    def run(path: str) -> tuple[str, int, str]:
        completed = subprocess.run([*command, path], capture_output=True, text=True, errors="replace", check=False)
        return path, completed.returncode, completed.stdout + completed.stderr

    failures = 0
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for path, code, output in pool.map(run, sources):
            reported = [line for line in output.splitlines() if re.search(r": (warning|error): ", line)]
            # A finding inside a system or SDK header is not ours to fix; the gate holds first-party code.
            findings = [line for line in reported if line.lower().startswith(str(ENGINE_ROOT).lower())]
            if findings or (code != 0 and not reported):
                failures += 1
                print(f"--- {path}")
                print("\n".join(findings) if findings else output.strip())
    print(f"clang-tidy gate: {len(sources)} sources, {len(checks)} checks, {failures} with findings")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

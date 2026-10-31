from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "sources"
INCLUDE = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)
SUFFIXES = {".cpp", ".hpp", ".h", ".inl"}


def _includes(directory: Path) -> list[tuple[Path, str]]:
    found: list[tuple[Path, str]] = []
    for path in sorted(directory.rglob("*")):
        if path.suffix not in SUFFIXES or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        found.extend((path, target) for target in INCLUDE.findall(text))
    return found


def _violations(directory: Path, forbidden_prefixes: tuple[str, ...], allowed: frozenset[str] = frozenset()) -> list[str]:
    result: list[str] = []
    for path, target in _includes(directory):
        relative = path.relative_to(ROOT).as_posix()
        if relative in allowed:
            continue
        if target.startswith(forbidden_prefixes):
            result.append(f"{relative} includes {target}")
    return result


class ModuleBoundaryTests(unittest.TestCase):
    """The direction of dependencies between the engine, the renderer, the editor and the optional libraries."""

    def test_sources_are_found(self) -> None:
        self.assertGreater(len(_includes(SOURCES / "renderer")), 100)

    def test_renderer_does_not_include_the_editor(self) -> None:
        self.assertEqual([], _violations(SOURCES / "renderer", ("kb/editor", "editor/")))

    def test_engine_core_does_not_include_the_renderer_or_the_editor(self) -> None:
        # The pack validator is a tool built on top of the renderer, not part of the runtime core.
        allowed = frozenset({"sources/engine/tools/RuntimeAssetPackValidator.cpp"})
        self.assertEqual([], _violations(SOURCES / "engine", ("kb/render", "kb/editor", "editor/"), allowed))

    def test_network_and_http_libraries_stand_alone(self) -> None:
        for library in ("net", "http"):
            self.assertEqual([], _violations(SOURCES / library, ("kb/render", "kb/editor", "editor/", "bgfx/")))

    def test_particle_runtime_does_not_include_the_editor(self) -> None:
        violations = [
            line for line in _violations(SOURCES / "plugins" / "21kb_particle", ("kb/editor", "editor/"))
            if "/editor/" not in line.split(" includes ")[0] and "/tests/" not in line.split(" includes ")[0]
        ]
        self.assertEqual([], violations)

    def test_the_checker_finds_a_violation(self) -> None:
        # Guards the guard: an include that is forbidden must be reported.
        self.assertTrue(_violations(SOURCES / "renderer", ("kb/render",)))

    def test_angle_brackets_do_not_bypass_module_boundaries(self) -> None:
        self.assertEqual(INCLUDE.findall('#include <kb/editor/Panel.hpp>\n#include "kb/render/Renderer.hpp"'),
                         ['kb/editor/Panel.hpp', 'kb/render/Renderer.hpp'])


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX = ROOT / "docs" / "renderer_readiness_matrix.md"
SELECTORS = ROOT / "sources" / "renderer" / "tests" / "RendererTests.cpp"
CMAKE_FILES = sorted((ROOT / "sources").rglob("CMakeLists.txt"))


def _rows() -> list[list[str]]:
    rows: list[list[str]] = []
    for line in MATRIX.read_text(encoding="utf-8").splitlines():
        if not line.startswith("|") or line.startswith("| ---") or line.startswith("| Feature"):
            continue
        rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows


def _ctest_names() -> set[str]:
    names: set[str] = set()
    for path in CMAKE_FILES:
        text = path.read_text(encoding="utf-8", errors="replace")
        names.update(re.findall(r"add_test\(\s*NAME\s+(\w+)", text))
        names.update(re.findall(r"add_test\(\s*(?!NAME\b)(\w+)", text))
    return names


class RendererReadinessMatrixTests(unittest.TestCase):
    """Every feature row names real tests and says what is not proven."""

    def test_matrix_has_rows(self) -> None:
        self.assertGreaterEqual(len(_rows()), 15)

    def test_every_row_says_what_is_not_proven(self) -> None:
        for row in _rows():
            self.assertEqual(7, len(row), f"malformed row: {row}")
            self.assertTrue(row[6], f"{row[0]} has no 'not proven' entry")

    def test_named_ctest_cases_exist(self) -> None:
        known = _ctest_names()
        for row in _rows():
            for name in re.findall(r"`ctest:(\w+)`", row[5]):
                # Sharded cases are registered in a loop as "<prefix>${index}", so the prefix stands for them.
                sharded = any(known_name.endswith("_") and re.fullmatch(re.escape(known_name) + r"\d+", name) for known_name in known)
                self.assertTrue(name in known or sharded, f"{row[0]}: CTest case {name} is not registered")

    def test_named_selectors_exist(self) -> None:
        source = SELECTORS.read_text(encoding="utf-8")
        for row in _rows():
            for name in re.findall(r"`selector:([\w-]+)`", row[5]):
                self.assertIn(f'"{name}"', source, f"{row[0]}: selector {name} is not handled by kb_renderer_tests")

    def test_every_row_has_evidence_or_says_none(self) -> None:
        for row in _rows():
            self.assertTrue(row[5] == "none" or "`" in row[5], f"{row[0]} lists no evidence")


if __name__ == "__main__":
    unittest.main()

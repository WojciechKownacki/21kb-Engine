from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CAPABILITY_REPORT = ROOT / "sources" / "renderer" / "src" / "RendererCapabilityReport.cpp"
PUBLIC_DOCS = (ROOT / "README.md", *sorted((ROOT / "docs").glob("*.md")))


def _hard_coded_unsupported(source: str) -> set[str]:
    return set(re.findall(r"report\.(\w+)\s*=\s*false\s*;", source))


class RendererClaimTests(unittest.TestCase):
    """The public description of the renderer may not promise what the capability report denies."""

    def setUp(self) -> None:
        self.unsupported = _hard_coded_unsupported(CAPABILITY_REPORT.read_text(encoding="utf-8"))

    def _claims(self, pattern: str) -> list[str]:
        lines: list[str] = []
        for document in PUBLIC_DOCS:
            for line in document.read_text(encoding="utf-8").splitlines():
                if re.search(pattern, line, re.IGNORECASE):
                    lines.append(f"{document.name}: {line.strip()}")
        return lines

    def test_capability_report_is_parsed(self) -> None:
        self.assertIn("gpuDrivenIndirectSubmitSupported", self.unsupported)

    def test_indirect_and_meshlet_submit_are_only_described_as_missing(self) -> None:
        for capability, pattern in (
            ("gpuDrivenIndirectSubmitSupported", r"indirect (draw|submit)"),
            ("gpuDrivenMeshletSubmitSupported", r"meshlet"),
        ):
            if capability not in self.unsupported:
                continue
            for line in self._claims(pattern):
                self.assertIn("not implemented", line.casefold(), f"{capability} is false but: {line}")

    def test_rendering_is_not_called_gpu_driven_without_gpu_submission(self) -> None:
        if "gpuDrivenIndirectSubmitSupported" not in self.unsupported:
            return
        self.assertEqual([], self._claims(r"gpu[- ]driven"))


if __name__ == "__main__":
    unittest.main()

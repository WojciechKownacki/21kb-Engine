from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github" / "workflows" / "ci.yml"
MATRIX_DOC = ROOT / "docs" / "library_support_matrix.md"


def _workflow_matrix(text: str) -> dict[str, dict[str, str]]:
    """The build-and-test matrix entries, keyed by job name."""
    entries: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for line in text.splitlines():
        entry = re.fullmatch(r" {10}- name: (.+)", line)
        if entry is not None:
            current = {}
            entries[entry.group(1).strip()] = current
            continue
        field = re.fullmatch(r" {12}(\w+): (.*)", line)
        if current is not None and field is not None:
            current[field.group(1)] = field.group(2).strip().strip('"')
        elif current is not None and not line.startswith(" " * 12):
            current = None
    return entries


def _workflow_job_names(text: str) -> dict[str, tuple[str, str]]:
    """Every job id with its display name and runner."""
    jobs: dict[str, tuple[str, str]] = {}
    in_jobs = False
    job_id = ""
    name = ""
    for line in text.splitlines():
        if line == "jobs:":
            in_jobs = True
            continue
        if not in_jobs:
            continue
        job = re.fullmatch(r"  ([A-Za-z0-9_-]+):", line)
        if job is not None:
            job_id = job.group(1)
            name = ""
            continue
        named = re.fullmatch(r"    name: (.+)", line)
        if named is not None and job_id:
            name = named.group(1).strip()
        runner = re.fullmatch(r"    runs-on: (.+)", line)
        if runner is not None and job_id:
            jobs[job_id] = (name, runner.group(1).strip())
    return jobs


def _table_rows(text: str, heading: str) -> list[list[str]]:
    section = text.split(f"## {heading}", 1)
    if len(section) != 2:
        raise AssertionError(f"support matrix has no '{heading}' section")
    body = section[1].split("\n## ", 1)[0]
    rows: list[list[str]] = []
    for line in body.splitlines():
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if cells and (cells[0] == "CI job" or set(cells[0]) <= {"-", " "}):
            continue
        rows.append(cells)
    return rows


def _code_names(cell: str) -> set[str]:
    return set(re.findall(r"`([^`]+)`", cell))


class SupportMatrixTests(unittest.TestCase):
    def setUp(self) -> None:
        self.workflow = WORKFLOW.read_text(encoding="utf-8")
        self.matrix = _workflow_matrix(self.workflow)
        self.doc = MATRIX_DOC.read_text(encoding="utf-8")

    def test_workflow_is_parsed(self) -> None:
        self.assertGreaterEqual(len(self.matrix), 1)
        for name, fields in self.matrix.items():
            self.assertIn("targets", fields, name)
            self.assertIn("os", fields, name)

    def test_every_build_row_is_a_ci_job_with_exactly_its_targets(self) -> None:
        rows = _table_rows(self.doc, "Build and test jobs")
        documented = {row[0]: row for row in rows}
        self.assertEqual(len(documented), len(rows), "a CI job is listed twice")
        self.assertEqual(set(self.matrix), set(documented), "documented jobs differ from the CI matrix")
        for name, fields in self.matrix.items():
            row = documented[name]
            self.assertEqual({fields["os"]}, _code_names(row[1]), f"{name}: runner differs from CI")
            ci_targets = {target for target in fields["targets"].split(";") if target}
            self.assertEqual(ci_targets, _code_names(row[4]), f"{name}: targets differ from CI")

    def test_other_jobs_match_the_workflow(self) -> None:
        jobs = _workflow_job_names(self.workflow)
        others = {name: runner for job_id, (name, runner) in jobs.items() if job_id != "build-and-test"}
        rows = _table_rows(self.doc, "Other jobs")
        self.assertEqual(set(others), {row[0] for row in rows}, "documented extra jobs differ from CI")
        for row in rows:
            self.assertEqual({others[row[0]]}, _code_names(row[1]), f"{row[0]}: runner differs from CI")

    def test_no_platform_is_claimed_without_a_ci_runner(self) -> None:
        runners = " ".join(fields["os"] for fields in self.matrix.values())
        claimed = self.doc.split("## Not covered by CI", 1)[0]
        if "macos" not in runners:
            self.assertNotIn("macos", claimed.casefold(), "macOS is described as tested without a CI job")


if __name__ == "__main__":
    unittest.main()

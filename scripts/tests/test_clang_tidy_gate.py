"""Regression coverage for static-analysis scope and truthful empty selections."""

import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SCRIPT = Path(__file__).resolve().parents[1] / "clang_tidy_gate.py"
SPEC = importlib.util.spec_from_file_location("clang_tidy_gate", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class ClangTidyGateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / ".clang-tidy").write_text("WarningsAsErrors: 'bugprone-use-after-move'\n", encoding="utf-8")
        self.build = self.root / "build"
        self.build.mkdir()
        self.sources = [self.root / "sources/One.cpp", self.root / "sources/Two.cpp"]
        for source in self.sources:
            source.parent.mkdir(exist_ok=True)
            source.write_text("int value;\n", encoding="utf-8")
        self.database(self.sources)
        self.addCleanup(patch.stopall)
        patch.object(gate, "ENGINE_ROOT", self.root).start()
        self.tidy = patch.object(gate, "find_clang_tidy", return_value="clang-tidy").start()
        self.run = patch.object(gate.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, "", "")).start()

    def database(self, paths):
        (self.build / "compile_commands.json").write_text(json.dumps([
            dict(directory=str(self.root), file=str(path), command="clang++ -c " + str(path))
            for path in paths
        ]), encoding="utf-8")

    def invoke(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                code = gate.main(["--build-dir", str(self.build), "--jobs", "1", *args])
            except SystemExit as error:
                code = error.code
        return code, out.getvalue(), err.getvalue()

    def require_rejected(self, *args):
        code, out, err = self.invoke(*args)
        self.assertEqual(code, 2)
        self.assertIn("no first-party sources", err)
        self.assertNotIn("with findings", out)
        self.run.assert_not_called()

    def test_empty_database_is_not_success(self):
        self.database([])
        self.require_rejected()

    def test_only_dependency_sources_is_not_success(self):
        self.database([self.root / "third_party/sources/Dependency.cpp"])
        self.require_rejected()

    def test_unmatched_filter_is_not_success(self):
        self.require_rejected("--filter", "MissingSource")

    def test_unmatched_filter_file_is_not_success(self):
        pattern = self.root / "filter.txt"
        pattern.write_text("MissingSource\n", encoding="utf-8")
        self.require_rejected("--filter-file", str(pattern))

    def test_invalid_filter_has_cli_diagnostic(self):
        code, out, err = self.invoke("--filter", "[")
        self.assertEqual(code, 2)
        self.assertIn("invalid source filter", err)
        self.assertNotIn("with findings", out)
        self.run.assert_not_called()

    def test_filter_executes_selected_source(self):
        code, out, err = self.invoke("--filter", r"One\.cpp$")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("1 sources, 1 checks, 0 with findings", out)
        self.assertEqual(self.run.call_args.args[0][-1], str(self.sources[0].resolve()))
        self.assertEqual(self.run.call_count, 1)

    def test_default_executes_all_first_party_sources(self):
        code, out, err = self.invoke()
        self.assertEqual((code, err), (0, ""))
        self.assertIn("2 sources, 1 checks, 0 with findings", out)
        self.assertEqual(self.run.call_count, 2)

    def test_unchanged_scope_reports_skip_without_claiming_analysis(self):
        self.run.return_value = subprocess.CompletedProcess([], 0, "docs/input.md\n", "")
        code, out, err = self.invoke("--changed-since", "HEAD~1")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("skipped", out)
        self.assertIn("no changed first-party sources", out)
        self.assertNotIn("with findings", out)
        self.assertEqual(self.run.call_count, 1)  # git only
        self.tidy.assert_not_called()

    def test_changed_source_executes_only_that_source(self):
        self.run.side_effect = [subprocess.CompletedProcess([], 0, "sources/Two.cpp\n", ""),
                                subprocess.CompletedProcess([], 0, "", "")]
        code, out, err = self.invoke("--changed-since", "HEAD~1")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("1 sources, 1 checks, 0 with findings", out)
        self.assertEqual(self.run.call_args.args[0][-1], str(self.sources[1].resolve()))

    def test_changed_header_keeps_all_sources(self):
        self.run.side_effect = [subprocess.CompletedProcess([], 0, "sources/Input.hpp\n", ""),
                                subprocess.CompletedProcess([], 0, "", ""),
                                subprocess.CompletedProcess([], 0, "", "")]
        code, out, err = self.invoke("--changed-since", "HEAD~1")
        self.assertEqual((code, err), (0, ""))
        self.assertIn("2 sources, 1 checks, 0 with findings", out)
        self.assertEqual(self.run.call_count, 3)


if __name__ == "__main__":
    unittest.main()

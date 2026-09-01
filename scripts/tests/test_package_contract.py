from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

from package_contract import (  # noqa: E402
    PackagingError,
    atomic_publish,
    run_checked,
    seal_unit,
    source_tree_fingerprint,
    verify_unit,
)


class PackageContractTests(unittest.TestCase):
    def test_source_fingerprint_normalizes_text_eols_but_not_binary_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            windows_text = root / "windows-text"
            linux_text = root / "linux-text"
            windows_text.mkdir()
            linux_text.mkdir()
            (windows_text / "Source.cpp").write_bytes(b"line one\r\nline two\r\n")
            (linux_text / "Source.cpp").write_bytes(b"line one\nline two\n")
            self.assertEqual(
                source_tree_fingerprint(windows_text),
                source_tree_fingerprint(linux_text),
            )

            windows_binary = root / "windows-binary"
            linux_binary = root / "linux-binary"
            windows_binary.mkdir()
            linux_binary.mkdir()
            (windows_binary / "payload.bin").write_bytes(b"opaque\r\nbytes")
            (linux_binary / "payload.bin").write_bytes(b"opaque\nbytes")
            self.assertNotEqual(
                source_tree_fingerprint(windows_binary),
                source_tree_fingerprint(linux_binary),
            )

    def test_sealed_unit_rejects_added_and_modified_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text) / "unit"
            root.mkdir()
            (root / "Game.kbpack").write_bytes(b"pack")
            seal_unit(root, {"target": "Windows.x64"})
            self.assertEqual("Windows.x64", verify_unit(root)["target"])

            (root / "unknown.dll").write_bytes(b"extra")
            with self.assertRaisesRegex(PackagingError, "differs from manifest"):
                verify_unit(root)
            (root / "unknown.dll").unlink()

            (root / "Game.kbpack").write_bytes(b"changed")
            with self.assertRaisesRegex(PackagingError, "integrity verification"):
                verify_unit(root)

    def test_receipt_cannot_be_detached_from_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text) / "unit"
            root.mkdir()
            (root / "game.wasm").write_bytes(b"\0asm")
            seal_unit(root, {"target": "WebGPU.wasm32"})
            manifest = json.loads((root / "package.manifest.json").read_text(encoding="utf-8"))
            manifest["files"][0]["size"] += 1
            (root / "package.manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(PackagingError, "receipt does not match"):
                verify_unit(root)

    def test_atomic_publish_replaces_one_complete_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            old = root / "published"
            old.mkdir()
            (old / "old.exe").write_bytes(b"old")
            seal_unit(old, {"target": "Windows.x64", "generation": 1})
            candidate = root / "candidate"
            candidate.mkdir()
            (candidate / "new.exe").write_bytes(b"new")
            seal_unit(candidate, {"target": "Windows.x64", "generation": 2})

            atomic_publish(candidate, old)

            self.assertFalse(candidate.exists())
            self.assertFalse((old / "old.exe").exists())
            self.assertEqual(b"new", (old / "new.exe").read_bytes())
            self.assertEqual(2, verify_unit(old)["generation"])
            self.assertEqual([], list(root.glob(".published.previous-*")))

    def test_atomic_publish_never_replaces_an_unmanaged_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            destination = root / "selected-folder"
            destination.mkdir()
            (destination / "personal-file.txt").write_text("keep", encoding="utf-8")
            candidate = root / "candidate"
            candidate.mkdir()
            (candidate / "game.exe").write_bytes(b"game")
            seal_unit(candidate, {"target": "Windows.x64"})
            with self.assertRaisesRegex(PackagingError, "manifest or receipt is missing"):
                atomic_publish(candidate, destination)
            self.assertEqual("keep", (destination / "personal-file.txt").read_text(encoding="utf-8"))
            self.assertTrue(candidate.is_dir())

    def test_package_tree_rejects_symbolic_links(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text) / "unit"
            root.mkdir()
            outside = Path(temporary_text) / "outside"
            outside.write_bytes(b"outside")
            try:
                (root / "link").symlink_to(outside)
            except (OSError, NotImplementedError):
                self.skipTest("symbolic links are unavailable for this account")
            with self.assertRaisesRegex(PackagingError, "not a regular file|symbolic links"):
                seal_unit(root, {"target": "Linux.x64"})

    def test_silent_process_timeout_kills_the_child(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            with self.assertRaisesRegex(PackagingError, "timed out"):
                run_checked(
                    [sys.executable, "-c", "import time; time.sleep(30)"],
                    cwd=Path(temporary_text),
                    timeout_seconds=0.2,
                )


if __name__ == "__main__":
    unittest.main()

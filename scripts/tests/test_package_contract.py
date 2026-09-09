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
    canonical_json_bytes,
    create_manifest,
    payload_manifest_sha256,
    run_checked,
    runtime_first_frame_observation,
    seal_unit,
    sha256_bytes,
    source_tree_fingerprint,
    verify_unit,
)


class PackageContractTests(unittest.TestCase):
    def test_source_fingerprint_ignores_only_known_generated_build_files(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            source = root / "source"
            source.mkdir()
            code = source / "Runtime.cpp"
            code.write_text("int runtime = 1;\n", encoding="utf-8")
            baseline = source_tree_fingerprint(source)

            (source / ".GRADLE").mkdir()
            (source / ".GRADLE/cache.bin").write_bytes(b"cache")
            (source / "nested/CMakeFiles").mkdir(parents=True)
            (source / "nested/CMakeFiles/compiler.cmake").write_bytes(b"generated")
            (source / "nested/cmake_install.cmake").write_bytes(b"generated")
            (source / "nested/Project.VCXPROJ").write_bytes(b"generated")
            (source / "nested/Project.vcxproj.filters").write_bytes(b"generated")
            self.assertEqual(baseline, source_tree_fingerprint(source))

            code.write_text("int runtime = 2;\n", encoding="utf-8")
            self.assertNotEqual(baseline, source_tree_fingerprint(source))

    def test_package_manifest_keeps_generated_looking_payload_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "CMakeFiles").mkdir()
            (root / "CMakeFiles/payload.bin").write_bytes(b"payload")
            (root / "game.vcxproj").write_bytes(b"payload")
            seal_unit(root, {"target": "Android.ETC2.arm64"})
            manifest = json.loads((root / "package.manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(
                ["CMakeFiles/payload.bin", "game.vcxproj"],
                [entry["path"] for entry in manifest["files"]],
            )

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
            seal_unit(root, {"target": "Android.ASTC.arm64"})
            receipt = verify_unit(root)
            self.assertEqual("Android.ASTC.arm64", receipt["target"])

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
            seal_unit(root, {"target": "Android.ETC2.arm64"})
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
            legacy_manifest = canonical_json_bytes(create_manifest(old))
            (old / "package.manifest.json").write_bytes(legacy_manifest)
            (old / "package.receipt.json").write_bytes(canonical_json_bytes({
                "schema": 1,
                "target": "Windows.x64",
                "generation": 1,
                "manifestSha256": sha256_bytes(legacy_manifest),
            }))
            candidate = root / "candidate"
            candidate.mkdir()
            (candidate / "new.exe").write_bytes(b"new")
            candidate_sha256 = payload_manifest_sha256(candidate)
            probe, observed = runtime_first_frame_observation("Windows.x64")
            seal_unit(candidate, {"target": "Windows.x64", "generation": 2}, runtime_first_frame={
                "schema": 1,
                "probe": probe,
                "observed": observed,
                "payloadManifestSha256": candidate_sha256,
            })

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
            seal_unit(candidate, {"target": "Android.ASTC.arm64"})
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

    def test_seal_rejects_payload_changed_after_first_frame_without_writing_envelope(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            payload = root / "Game.exe"
            payload.write_bytes(b"probed")
            probed_manifest_sha256 = payload_manifest_sha256(root)
            probe, observed = runtime_first_frame_observation("Windows.x64")
            evidence = {
                "schema": 1,
                "probe": probe,
                "observed": observed,
                "payloadManifestSha256": probed_manifest_sha256,
            }
            payload.write_bytes(b"changed")

            with self.assertRaisesRegex(PackagingError, "changed after its runtime first-frame probe"):
                seal_unit(
                    root,
                    {"target": "Windows.x64"},
                    runtime_first_frame=evidence,
                )
            self.assertFalse((root / "package.manifest.json").exists())
            self.assertFalse((root / "package.receipt.json").exists())

    def test_runtime_first_frame_contract_is_exact_for_all_targets(self) -> None:
        self.assertEqual(
            {
                "Windows.x64": ("native-stdout", "frames=1 rendered=1 shutdown=clean"),
                "Linux.x64": ("linux-build-receipt", "frames=1 rendered=1 shutdown=clean"),
                "Android.ASTC.arm64": (
                    "android-logcat", "profile=Android.ASTC.arm64 first-frame=rendered",
                ),
                "Android.ETC2.arm64": (
                    "android-logcat", "profile=Android.ETC2.arm64 first-frame=rendered",
                ),
                "WebGL.wasm32": ("browser-fragment", "kb-ready-webgl"),
                "WebGPU.wasm32": ("browser-fragment", "kb-ready-webgpu"),
            },
            {
                target: runtime_first_frame_observation(target)
                for target in (
                    "Windows.x64", "Linux.x64", "Android.ASTC.arm64",
                    "Android.ETC2.arm64", "WebGL.wasm32", "WebGPU.wasm32",
                )
            },
        )

    def test_seal_rejects_missing_unknown_and_wrong_first_frame_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "Game.wasm").write_bytes(b"payload")
            payload_sha256 = payload_manifest_sha256(root)
            webgl_probe, webgl_observed = runtime_first_frame_observation("WebGL.wasm32")
            webgpu_probe, webgpu_observed = runtime_first_frame_observation("WebGPU.wasm32")

            with self.assertRaisesRegex(PackagingError, "requires runtime first-frame evidence"):
                seal_unit(root, {"target": "WebGL.wasm32"})
            with self.assertRaisesRegex(PackagingError, "unsupported target"):
                seal_unit(root, {"target": "Unknown"})
            with self.assertRaisesRegex(PackagingError, "unsupported target"):
                seal_unit(root, {})
            with self.assertRaisesRegex(PackagingError, "does not match the package target"):
                seal_unit(root, {"target": "WebGL.wasm32"}, runtime_first_frame={
                    "schema": 1,
                    "probe": webgpu_probe,
                    "observed": webgpu_observed,
                    "payloadManifestSha256": payload_sha256,
                })
            with self.assertRaisesRegex(PackagingError, "does not match the package target"):
                seal_unit(root, {"target": "WebGL.wasm32"}, runtime_first_frame={
                    "schema": 1,
                    "probe": webgl_probe,
                    "observed": webgl_observed,
                    "payloadManifestSha256": payload_sha256,
                    "extra": True,
                })
            self.assertFalse((root / "package.manifest.json").exists())
            self.assertFalse((root / "package.receipt.json").exists())

    def test_verify_rejects_tampered_first_frame_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "Game.exe").write_bytes(b"payload")
            payload_sha256 = payload_manifest_sha256(root)
            probe, observed = runtime_first_frame_observation("Windows.x64")
            seal_unit(root, {"target": "Windows.x64"}, runtime_first_frame={
                "schema": 1,
                "probe": probe,
                "observed": observed,
                "payloadManifestSha256": payload_sha256,
            })
            receipt_path = root / "package.receipt.json"
            receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
            receipt["runtimeFirstFrame"]["extra"] = True
            receipt_path.write_text(json.dumps(receipt), encoding="utf-8")
            with self.assertRaisesRegex(PackagingError, "invalid runtime first-frame evidence"):
                verify_unit(root)

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

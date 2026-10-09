from __future__ import annotations

import argparse
import io
import json
import os
import re
import secrets
import shutil
import stat
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import package_game  # noqa: E402
import package_linux_guest  # noqa: E402
import package_contract  # noqa: E402
import windows_authenticode  # noqa: E402
import windows_pe_symbols  # noqa: E402
from package_contract import PackagingError, seal_unit  # noqa: E402
import third_party_notices  # noqa: E402
from windows_pe_resources import (  # noqa: E402
    TRUST_ANCHOR_RESOURCE_ID,
    _version_resource,
    _version_tuple,
    apply_windows_resources,
)


def write_pe_with_pdb_reference(path: Path, pdb_name: str | None, guid: bytes, age: int) -> None:
    """A minimal PE32+ image whose debug directory names a PDB the way the MSVC linker does."""
    data = bytearray(0x400)
    data[0:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 0xF0, 0x22)
    optional = 0x84 + 20
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 108, 16)
    section = optional + 0xF0
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 0x200, 0x1000, 0x200, 0x200)
    if pdb_name is not None:
        struct.pack_into("<II", data, optional + 112 + 6 * 8, 0x1000, 28)
        record = b"RSDS" + guid + struct.pack("<I", age) + b"C:\\Build\\bin\\" + pdb_name.encode() + b"\0"
        struct.pack_into("<IIHHIIII", data, 0x200, 0, 0, 0, 0, 2, len(record), 0x1020, 0x220)
        data[0x220:0x220 + len(record)] = record
    path.write_bytes(bytes(data))


def write_pdb(path: Path, guid: bytes) -> None:
    """A minimal MSF 7 PDB: superblock, directory and the information stream carrying the GUID."""
    block_size = 512
    info = struct.pack("<III", 20000404, 0x5EED, 1) + guid
    directory = struct.pack("<III", 2, 0, len(info)) + struct.pack("<I", 5)
    blocks = [bytearray(block_size) for _ in range(6)]
    superblock = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0" + struct.pack(
        "<6I", block_size, 1, len(blocks), len(directory), 0, 3
    )
    blocks[0][:len(superblock)] = superblock
    blocks[3][:4] = struct.pack("<I", 4)
    blocks[4][:len(directory)] = directory
    blocks[5][:len(info)] = info
    path.write_bytes(b"".join(blocks))


def built_authenticode_signer() -> Path | None:
    """kb_authenticode_signer from a build tree of this checkout, if one was built."""
    configured = os.environ.get("KB_AUTHENTICODE_SIGNER")
    if configured:
        return Path(configured) if Path(configured).is_file() else None
    return next(iter(sorted(SCRIPTS.parent.glob("build/*/bin/kb_authenticode_signer.exe"))), None)


def create_throwaway_certificate(directory: Path, password: str) -> tuple[Path, str]:
    """A self-signed code-signing certificate made in memory by .NET and exported to a PFX.

    Nothing is added to a certificate store and no trust changes; the password reaches
    PowerShell on standard input.
    """
    pfx = directory / "throwaway.pfx"
    script = (
        "$ErrorActionPreference='Stop';"
        "$password=[Console]::In.ReadLine();"
        "$key=[System.Security.Cryptography.RSACng]::new(2048);"
        "$request=[System.Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=21kb Packaging Test',$key,"
        "[System.Security.Cryptography.HashAlgorithmName]::SHA256,[System.Security.Cryptography.RSASignaturePadding]::Pkcs1);"
        "$usage=[System.Security.Cryptography.OidCollection]::new();"
        "[void]$usage.Add([System.Security.Cryptography.Oid]::new('1.3.6.1.5.5.7.3.3'));"
        "$request.CertificateExtensions.Add("
        "[System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]::new($usage,$false));"
        "$certificate=$request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddMinutes(-5),[DateTimeOffset]::UtcNow.AddDays(1));"
        f"[System.IO.File]::WriteAllBytes('{pfx}',"
        "$certificate.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx,$password));"
        "Write-Output $certificate.Thumbprint"
    )
    powershell = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    result = subprocess.run(
        [str(powershell), "-NoProfile", "-NonInteractive", "-Command", script],
        input=password + "\n", capture_output=True, text=True, timeout=120, check=True,
    )
    return pfx, result.stdout.strip().splitlines()[-1]


def seal_with_first_frame(root: Path, fields: dict[str, object]) -> None:
    probe, observed = package_contract.runtime_first_frame_observation(fields["target"])
    seal_unit(
        root,
        fields,
        runtime_first_frame={
            "schema": 1,
            "probe": probe,
            "observed": observed,
            "payloadManifestSha256": package_contract.payload_manifest_sha256(root),
        },
    )


class PackageGameTests(unittest.TestCase):
    @staticmethod
    def _windows_launch_package(root: Path) -> argparse.Namespace:
        build_root = root / "build"
        output = root / "published"
        output.mkdir()
        (output / "Game.exe").write_bytes(b"player")
        seal_with_first_frame(output, {
            "target": "Windows.x64",
            "configuration": "Development",
            "inputs": {"engineSha256": "0" * 64},
        })
        return argparse.Namespace(
            build_root=build_root,
            output=output,
            target="Windows.x64",
            executable_name="Game",
        )

    @staticmethod
    def _web_launch_package(root: Path) -> argparse.Namespace:
        build_root = root / "build"
        output = root / "published-web"
        output.mkdir()
        (output / "Game.html").write_bytes(b"<html></html>")
        seal_with_first_frame(output, {
            "target": "WebGL.wasm32",
            "configuration": "Development",
            "inputs": {"engineSha256": "0" * 64},
        })
        return argparse.Namespace(
            build_root=build_root,
            output=output,
            target="WebGL.wasm32",
            executable_name="Game",
        )

    def test_required_target_matrix_is_exact(self) -> None:
        self.assertIs(package_contract.PACKAGE_TARGETS, package_game.TARGETS)
        self.assertEqual(
            {
                "Windows.x64",
                "Linux.x64",
                "Android.ASTC.arm64",
                "Android.ETC2.arm64",
                "WebGL.wasm32",
                "WebGPU.wasm32",
            },
            set(package_game.TARGETS),
        )
        self.assertEqual("ASTC", package_game.TARGETS["Android.ASTC.arm64"].texture_family)
        self.assertEqual("ETC2", package_game.TARGETS["Android.ETC2.arm64"].texture_family)
        self.assertEqual("SPIR-V+ESSL", package_game.TARGETS["Android.ETC2.arm64"].shader_format)
        self.assertEqual("BC1-BC3+ETC2", package_game.TARGETS["WebGL.wasm32"].texture_family)
        self.assertEqual("WGSL", package_game.TARGETS["WebGPU.wasm32"].shader_format)
        self.assertIsNotNone(package_game._ANDROID_ALIAS.fullmatch("release.key-1"))
        self.assertIsNone(package_game._ANDROID_ALIAS.fullmatch("release key"))

    def test_windows_reserved_executable_names_match_the_ui_contract(self) -> None:
        reserved = (
            "CON", "prn.exe", "Aux.data", "nul", "COM1", "com9.exe",
            "LPT1", "lpt9.bundle", "CONIN$", "CONOUT$", "con.txt", "NUL.package",
        )
        for name in reserved:
            self.assertTrue(package_game._is_windows_reserved_device_name(name), name)
            self.assertFalse(package_game._is_safe_executable_name(name, "Windows.x64"), name)
        self.assertTrue(package_game._is_safe_executable_name("con.txt", "WebGPU.wasm32"))
        for name in ("console", "com0", "com10", "lpt0", "lpt10", "Game.exe"):
            self.assertTrue(package_game._is_safe_executable_name(name, "Windows.x64"), name)

    def test_snapshot_is_frozen_and_excludes_transient_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text) / "project"
            (root / "Assets").mkdir(parents=True)
            (root / "Saved").mkdir()
            descriptor = root / "Project.21kbproject"
            descriptor.write_bytes(b"descriptor")
            (root / "Assets" / "scene.bin").write_bytes(b"scene")
            (root / "Saved" / "editor.log").write_bytes(b"log")
            destination = Path(temporary_text) / "snapshot"

            def reject_saved_visit(path: Path) -> bool:
                if "Saved" in path.parts:
                    raise AssertionError("excluded directory was visited")
                return False

            with mock.patch.object(package_contract, "_is_reparse", side_effect=reject_saved_visit):
                copied = package_game._copy_project_snapshot(descriptor, destination)
                fingerprint = package_game._project_source_fingerprint(descriptor)

            self.assertEqual(b"descriptor", copied.read_bytes())
            self.assertEqual(b"scene", (destination / "Assets/scene.bin").read_bytes())
            self.assertFalse((destination / "Saved").exists())
            self.assertEqual(64, len(fingerprint))

    def test_partitioned_world_sources_are_cooked_from_the_snapshot(self) -> None:
        # kb_cooker builds every partitioned world's cells inside the project it is given
        # before collecting assets. The package job must hand it the snapshot, carrying
        # the world file and its per-object files, so the build never touches the project.
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            project = root / "project"
            objects = project / "Assets" / "Worlds" / "Forest.objects"
            objects.mkdir(parents=True)
            descriptor = project / "Project.21kbproject"
            descriptor.write_bytes(b"descriptor")
            (project / "Assets" / "Worlds" / "Forest.21kbworld").write_bytes(b"world")
            (objects / ("0" * 32 + ".21kbobject")).write_bytes(b"object")
            job = root / "job"
            job.mkdir()
            snapshot = package_game._copy_project_snapshot(descriptor, job / "project")
            self.assertEqual(b"world", (job / "project" / "Assets" / "Worlds" / "Forest.21kbworld").read_bytes())
            self.assertEqual(b"object", (job / "project" / "Assets" / "Worlds" / "Forest.objects" / ("0" * 32 + ".21kbobject")).read_bytes())
            args = argparse.Namespace(
                build_root=root / "build",
                configuration="Development",
                engine_root=root / "engine",
                target="Linux.x64",
                pack_compression_level=package_game.DEFAULT_PACK_COMPRESSION_LEVEL,
            )
            with mock.patch.object(package_game, "_find_optional_build_tool", return_value=None), \
                    mock.patch.object(package_game, "run_checked") as run:
                package_game._cook(args, snapshot, job, root / "kb_cooker", root / "kb_runtime_asset_pack_validator")

            command = run.call_args_list[0].args[0]
            self.assertEqual(snapshot, command[command.index("--project") + 1])
            self.assertTrue(snapshot.is_relative_to(job))

    def test_world_regions_become_chunk_packs_after_explicit_chunks(self) -> None:
        # --pack-chunk-world-regions asks kb_cli for one rule per region of every world the cook
        # built into the snapshot, passing the explicit rules' prefixes so their files stay with
        # them, and splits the cooked pack with the explicit rules first.
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            (job / "cook").mkdir(parents=True)
            pack = job / "cook" / "Game.kbpack"
            pack.write_bytes(b"cooked")
            args = argparse.Namespace(
                target="Windows.x64",
                pack_compression_level=9,
                pack_chunk=["night=/Game/Worlds/Forest.cells/layer.night/"],
                pack_chunk_world_regions=True,
                patch_from=None,
                patch_level=None,
                encrypt_pack=False,
                snapshot_project=job / "project" / "Project.21kbproject",
            )
            package_game._validate_content_packaging(args)
            calls: list[list[str]] = []

            def fake_run(command, **_kwargs):
                argv = [os.fspath(value) for value in command]
                calls.append(argv)
                if argv[1:3] == ["world", "chunks"]:
                    return package_contract.ProcessResult(
                        "chunk Forest.r_0_0=/Game/Worlds/Forest.cells/base/r_0_0/,/Game/Worlds/Forest.cells/hlod/r_0_0/\n"
                        "chunk Forest.r_-1_2=/Game/Worlds/Forest.cells/base/r_-1_2/\n", 0.0)
                for name in ("Game.kbpack", "Game.night.kbpack", "Game.Forest.r_0_0.kbpack", "Game.Forest.r_-1_2.kbpack"):
                    (job / "cook" / name).write_bytes(b"pack")
                (job / "cook" / package_game.PACK_SET_INDEX).write_text("21kb-pack-set 1\n", encoding="utf-8")
                return package_contract.ProcessResult("", 0.0)

            with mock.patch.object(package_game, "_kb_cli", return_value=root / "kb_cli.exe"), \
                    mock.patch.object(package_game, "run_checked", side_effect=fake_run):
                pack_set = package_game._build_pack_set(args, root / "cmake", pack, job)

            chunks_call, split_call = calls
            self.assertEqual(job / "project", Path(chunks_call[chunks_call.index("--project") + 1]))
            self.assertEqual("/Game/Worlds/Forest.cells/layer.night/", chunks_call[chunks_call.index("--exclude") + 1])
            rules = [split_call[index + 1] for index, value in enumerate(split_call) if value == "--chunk"]
            self.assertEqual([
                "night=/Game/Worlds/Forest.cells/layer.night/",
                "Forest.r_0_0=/Game/Worlds/Forest.cells/base/r_0_0/,/Game/Worlds/Forest.cells/hlod/r_0_0/",
                "Forest.r_-1_2=/Game/Worlds/Forest.cells/base/r_-1_2/",
            ], rules)
            self.assertEqual(4, len(pack_set.new_packs))

    def test_world_region_chunks_are_windows_only_and_not_patches(self) -> None:
        base = dict(pack_compression_level=9, pack_chunk=[], pack_chunk_world_regions=True, patch_from=None,
                    patch_level=None, encrypt_pack=False)
        with self.assertRaises(package_game.PackagingError):
            package_game._validate_content_packaging(argparse.Namespace(target="Linux.x64", **base))

    def test_windows_cook_requests_custom_module_staging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            job.mkdir()
            args = argparse.Namespace(
                build_root=root / "build",
                configuration="Development",
                engine_root=root / "engine",
                target="Windows.x64",
                pack_compression_level=7,
            )
            with mock.patch.object(package_game, "_find_optional_build_tool", return_value=None), \
                    mock.patch.object(package_game, "run_checked") as run:
                pack = package_game._cook(
                    args,
                    root / "snapshot" / "Project.21kbproject",
                    job,
                    root / "kb_cooker.exe",
                    root / "kb_runtime_asset_pack_validator.exe",
                )

            command = run.call_args_list[0].args[0]
            output_option = command.index("--runtime-modules-output")
            self.assertEqual(job / "cook" / "RuntimeModules", command[output_option + 1])
            self.assertEqual("7", command[command.index("--pack-compression-level") + 1])
            self.assertEqual(job / "cook" / "Game.kbpack", pack)

    @staticmethod
    def _signing_args(root: Path, **overrides: object) -> argparse.Namespace:
        values: dict[str, object] = {
            "build_root": root / "build",
            "configuration": "Release",
            "engine_root": root / "engine",
            "target": "Windows.x64",
            "product_id": "Publisher.Game",
            "signing_key": None,
            "signing_broker": None,
            "encrypt_pack": False,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_pack_is_sealed_with_a_default_key_kept_outside_the_project(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            job.mkdir()
            kb_cli = root / "kb_cli.exe"
            args = self._signing_args(root, encrypt_pack=True)
            with mock.patch.dict(package_game.os.environ, {"KB_RELEASE_KEY_ROOT": str(root / "keys")}), \
                    mock.patch.object(package_game, "_build_targets") as build, \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "emit_diagnostic") as diagnostic, \
                    mock.patch.object(package_game, "run_checked") as run:
                package_game._sign_pack(args, Path("cmake.exe"), job / "Game.kbpack", job)

            key = root / "keys" / "Publisher.Game.kbkey"
            self.assertEqual(("kb_cli",), build.call_args.args[3])
            commands = [[str(value) for value in call.args[0]] for call in run.call_args_list]
            self.assertEqual([str(kb_cli), "keys", "generate", "--out", str(key)], commands[0])
            self.assertEqual("Warning", diagnostic.call_args.args[0])
            self.assertIn("Back it up", diagnostic.call_args.args[1])
            self.assertEqual(["keys", "content-key"], commands[1][1:3])
            content_key = str(job / "pack-content.key")
            self.assertEqual(
                [str(kb_cli), "pack", "sign", "--content-key", content_key, str(job / "Game.kbpack"), "--key", str(key)],
                commands[2],
            )
            self.assertEqual(
                [str(kb_cli), "keys", "anchor", "--product", "Publisher.Game", "--content-key", content_key,
                 "--out", str(job / "trust-anchor.bin"), "--key", str(key)],
                commands[3],
            )
            self.assertEqual(
                [str(kb_cli), "pack", "verify", "--anchor", str(job / "trust-anchor.bin"), str(job / "Game.kbpack")],
                commands[4],
            )
            self.assertEqual(job / "trust-anchor.bin", args.trust_anchor)
            self.assertEqual((kb_cli,), package_game._release_tools(args))

    def test_release_signing_broker_runs_key_operations_without_exposing_the_key(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            job.mkdir()
            kb_cli = root / "kb_cli.exe"
            broker = root / "broker.exe"
            requests: list[dict[str, object]] = []

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                if argv[0] == str(broker):
                    request = json.loads(Path(argv[argv.index("--request") + 1]).read_text(encoding="utf-8"))
                    requests.append(request)
                    Path(argv[argv.index("--response") + 1]).write_text(
                        json.dumps({"schema": 1, "session": request["session"], "succeeded": True}), encoding="utf-8"
                    )
                else:
                    self.assertNotIn("--key", argv)
                return mock.MagicMock()

            args = self._signing_args(root, signing_broker=broker)
            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "run_checked", side_effect=run):
                package_game._sign_pack(args, Path("cmake.exe"), job / "Game.kbpack", job)

            self.assertEqual(2, len(requests))
            self.assertEqual("kbReleaseSigning", requests[0]["kind"])
            self.assertIsNone(requests[0]["key"])
            self.assertEqual(["pack", "sign", str(job / "Game.kbpack")], requests[0]["arguments"])
            self.assertEqual("keys", requests[1]["arguments"][0])
            self.assertEqual((kb_cli, broker), package_game._release_tools(args))

            def refuse(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                if argv[0] == str(broker):
                    Path(argv[argv.index("--response") + 1]).write_text('{"schema": 1}', encoding="utf-8")
                return mock.MagicMock()

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "run_checked", side_effect=refuse):
                with self.assertRaisesRegex(PackagingError, "broker refused"):
                    package_game._sign_pack(args, Path("cmake.exe"), job / "Game.kbpack", job)

    def test_release_manifest_is_signed_over_the_finished_stage_and_verified(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            stage = root / "stage"
            signing = package_game.ReleaseSigning(root / "kb_cli.exe", root / "game.kbkey", None)
            args = self._signing_args(
                root, release_signing=signing, version="1.2.0 beta", release_number=1234, anti_rollback=True
            )
            with mock.patch.object(package_game, "run_checked") as run:
                package_game._sign_release(args, stage, job)
            commands = [[str(value) for value in call.args[0]] for call in run.call_args_list]
            self.assertEqual(
                [str(root / "kb_cli.exe"), "release", "sign", "--dir", str(stage), "--product", "Publisher.Game",
                 "--content-version", "1.2.0-beta", "--release", "1234", "--anti-rollback",
                 "--key", str(root / "game.kbkey")],
                commands[0],
            )
            self.assertEqual([str(root / "kb_cli.exe"), "release", "verify", str(stage)], commands[1])

            unsigned = self._signing_args(root)
            with mock.patch.object(package_game, "run_checked") as run:
                package_game._sign_release(unsigned, stage, job)
            run.assert_not_called()

    @staticmethod
    def _content_args(root: Path, **overrides: object) -> argparse.Namespace:
        values: dict[str, object] = {
            "build_root": root / "build",
            "configuration": "Release",
            "engine_root": root / "engine",
            "target": "Windows.x64",
            "product_id": "Publisher.Game",
            "signing_key": None,
            "signing_broker": None,
            "encrypt_pack": False,
            "release_number": 20,
            "pack_compression_level": package_game.DEFAULT_PACK_COMPRESSION_LEVEL,
            "pack_chunk": [],
            "pack_chunk_cells": [],
            "patch_from": None,
            "patch_level": None,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    @staticmethod
    def _previous_release(root: Path, release: int, index: str | None) -> Path:
        previous = root / "release-previous"
        previous.mkdir()
        (previous / "Game.kbpack").write_bytes(b"base pack")
        (previous / "release.kbmanifest").write_text(
            "21kb-release-manifest 1\nproduct Publisher.Game\ncontent-version 1.0\n"
            f"release {release}\nanti-rollback 1\nsignature {'0' * 128}\n",
            encoding="utf-8",
        )
        if index is not None:
            (previous / package_game.PACK_SET_INDEX).write_text(index, encoding="utf-8")
            for line in index.splitlines()[2:]:
                (previous / line.rsplit(" ", 1)[1]).write_bytes(line.encode("utf-8"))
        return previous

    def test_content_packaging_options_are_validated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            chunked = self._content_args(root, pack_chunk=["cell_0_0=/Game/Cells/0_0/,/Game/Layers/Night/"])
            package_game._validate_content_packaging(chunked)
            self.assertEqual([("cell_0_0", ("/Game/Cells/0_0/", "/Game/Layers/Night/"))], chunked.pack_chunk_rules)
            self.assertIsNone(chunked.patch_base_entries)
            previous = self._previous_release(
                root, 12, "21kb-pack-set 1\nbase Game.kbpack\nchunk cell Game.cell.kbpack\n"
                "patch 3 patch-0003 Game.patch-0003.kbpack\n"
            )
            for refused, message in (
                ({"pack_compression_level": 20}, "0 \\(off\\) to 19"),
                ({"pack_compression_level": -1}, "0 \\(off\\) to 19"),
                ({"pack_chunk": ["cell"]}, "LABEL="),
                ({"pack_chunk": ["../x=/Game/Cells/"]}, "LABEL="),
                ({"pack_chunk": ["cell=Game/Cells/"]}, "virtual path"),
                ({"pack_chunk": ["cell=/Game/../Secrets/"]}, "virtual path"),
                ({"pack_chunk": ["cell=/Game/A/", "cell=/Game/B/"]}, "its own label"),
                ({"pack_chunk": ["cell=/Game/A/"], "target": "WebGL.wasm32"}, "Windows and Linux players only"),
                ({"pack_chunk_cells": ["east=/Game/Worlds/Forest.21kbworld@2:0..1:0"]}, "out of order"),
                ({"pack_chunk_cells": ["east=Game/Worlds/Forest.21kbworld"]}, "LABEL=/Game/WORLD"),
                ({"pack_chunk_cells": ["east=/Game/Worlds/Forest.21kbworld#bad layer"]}, "LABEL=/Game/WORLD"),
                ({"pack_chunk_cells": ["east=/Game/Worlds/Forest.21kbscene"]}, "LABEL=/Game/WORLD"),
                ({"patch_level": 4}, "needs --patch-from"),
                ({"patch_from": previous, "target": "Android.ETC2.arm64"}, "Windows and Linux players only"),
                ({"patch_from": previous, "pack_chunk": ["cell=/Game/A/"]}, "keeps the chunks"),
                ({"patch_from": previous, "release_number": 12}, "higher than the patched release's 12"),
                ({"patch_from": previous, "patch_level": 3}, "higher than the release's patch level 3"),
                ({"patch_from": root}, "signed Windows or Linux release"),
            ):
                with self.assertRaisesRegex(PackagingError, message):
                    package_game._validate_content_packaging(self._content_args(root, **refused))

            patch = self._content_args(root, patch_from=previous)
            package_game._validate_content_packaging(patch)
            self.assertEqual(4, patch.patch_level)
            # A Linux release verifies its signed manifest like a Windows one, so it takes pack
            # sets and patches too.
            linux = self._content_args(root, patch_from=previous, target="Linux.x64")
            package_game._validate_content_packaging(linux)
            self.assertEqual(4, linux.patch_level)
            package_game._validate_content_packaging(self._content_args(root, target="Linux.x64", pack_chunk=["cell=/Game/A/"]))
            self.assertEqual(
                [("base", "", 0, "Game.kbpack"), ("chunk", "cell", 0, "Game.cell.kbpack"),
                 ("patch", "patch-0003", 3, "Game.patch-0003.kbpack")],
                patch.patch_base_entries,
            )
            (previous / "Game.cell.kbpack").unlink()
            with self.assertRaisesRegex(PackagingError, "missing Game.cell.kbpack"):
                package_game._validate_content_packaging(self._content_args(root, patch_from=previous))
            (previous / package_game.PACK_SET_INDEX).write_text("21kb-pack-set 1\nbase ../Game.kbpack\n", encoding="utf-8")
            with self.assertRaisesRegex(PackagingError, "unexpected file"):
                package_game._validate_content_packaging(self._content_args(root, patch_from=previous))

        parsed = package_game._parse_arguments([
            "--project", "missing", "--target", "Windows.x64", "--configuration", "Release", "--output", "out",
            "--engine-root", ".", "--build-root", "build", "--product-name", "Game", "--publisher", "Studio",
            "--version", "1.0", "--executable-name", "Game",
        ])
        self.assertEqual(9, parsed.pack_compression_level)
        self.assertEqual([], parsed.pack_chunk)
        self.assertIsNone(parsed.patch_from)

    def test_chunked_pack_set_is_split_sealed_and_staged_as_one_verified_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            cook = job / "cook"
            stage = root / "stage"
            cook.mkdir(parents=True)
            stage.mkdir()
            pack = cook / "Game.kbpack"
            pack.write_bytes(b"cooked")
            kb_cli = root / "kb_cli.exe"
            key = root / "game.kbkey"
            key.write_bytes(b"key")
            args = self._content_args(root, signing_key=key, pack_chunk=["cell_0_0=/Game/Cells/0_0/", "night=/Game/Layers/Night/"])
            package_game._validate_content_packaging(args)

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                if argv[1:3] == ["pack", "split"]:
                    self.assertEqual(b"cooked", Path(argv[-1]).read_bytes())
                    base = Path(argv[argv.index("--base") + 1])
                    base.write_bytes(b"base")
                    for spec in (argv[position + 1] for position, value in enumerate(argv) if value == "--chunk"):
                        base.with_name(f"Game.{spec.split('=')[0]}.kbpack").write_bytes(spec.encode("utf-8"))
                    Path(argv[argv.index("--index") + 1]).write_text(
                        "21kb-pack-set 1\nbase Game.kbpack\nchunk cell_0_0 Game.cell_0_0.kbpack\n"
                        "chunk night Game.night.kbpack\n", encoding="utf-8")
                return mock.MagicMock()

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "emit_diagnostic"), \
                    mock.patch.object(package_game, "run_checked", side_effect=run) as checked:
                pack_set = package_game._build_pack_set(args, Path("cmake.exe"), pack, job)
                package_game._sign_pack(args, Path("cmake.exe"), pack_set, job)
                package_game._stage_pack_set(args, pack, stage, job)

            commands = [[str(value) for value in call.args[0]] for call in checked.call_args_list]
            self.assertEqual(
                [str(kb_cli), "pack", "split", "--base", str(pack), "--level", "9", "--index",
                 str(cook / "Game.kbpackset"), "--chunk", "cell_0_0=/Game/Cells/0_0/", "--chunk",
                 "night=/Game/Layers/Night/", str(cook / "unsplit" / "Game.kbpack")],
                commands[0],
            )
            signed = [command[-3] for command in commands if command[1:3] == ["pack", "sign"]]
            verified = [command[-1] for command in commands if command[1:3] == ["pack", "verify"]]
            every_pack = [str(pack), str(cook / "Game.cell_0_0.kbpack"), str(cook / "Game.night.kbpack")]
            self.assertEqual(every_pack, signed)
            self.assertEqual(every_pack, verified)
            self.assertEqual(
                [str(kb_cli), "pack", "set-verify", "--anchor", str(job / "trust-anchor.bin"), str(stage / "Game.kbpackset")],
                commands[-1],
            )
            self.assertEqual(
                ["Game.cell_0_0.kbpack", "Game.kbpack", "Game.kbpackset", "Game.night.kbpack"],
                sorted(path.name for path in stage.iterdir()),
            )
            self.assertEqual(b"base", (stage / "Game.kbpack").read_bytes())

    def test_world_regions_are_split_into_chunk_packs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            cook = job / "cook"
            cook.mkdir(parents=True)
            pack = cook / "Game.kbpack"
            pack.write_bytes(b"cooked")
            kb_cli = root / "kb_cli.exe"
            args = self._content_args(
                root,
                pack_chunk=["night=/Game/Layers/Night/"],
                pack_chunk_cells=[
                    "forest_east=/Game/Worlds/Forest.21kbworld@1:-4..8:4",
                    "night=/Game/Worlds/Forest.21kbworld#night",
                    "forest_west=/Game/Worlds/Forest.21kbworld@-8:-4..0:4#(base),props",
                ],
            )
            package_game._validate_content_packaging(args)

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                base = Path(argv[argv.index("--base") + 1])
                base.write_bytes(b"base")
                for label in ("night", "forest_east", "forest_west"):
                    base.with_name(f"Game.{label}.kbpack").write_bytes(label.encode("utf-8"))
                Path(argv[argv.index("--index") + 1]).write_text("21kb-pack-set 1\nbase Game.kbpack\n", encoding="utf-8")
                return mock.MagicMock()

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "emit_diagnostic"), \
                    mock.patch.object(package_game, "run_checked", side_effect=run) as checked:
                pack_set = package_game._build_pack_set(args, Path("cmake.exe"), pack, job)

            command = [str(value) for value in checked.call_args.args[0]]
            self.assertEqual(
                ["--chunk", "night=/Game/Layers/Night/",
                 "--chunk-cells", "forest_east=/Game/Worlds/Forest.21kbworld@1:-4..8:4",
                 "--chunk-cells", "night=/Game/Worlds/Forest.21kbworld#night",
                 "--chunk-cells", "forest_west=/Game/Worlds/Forest.21kbworld@-8:-4..0:4#(base),props"],
                command[command.index("--index") + 2:-1],
            )
            self.assertEqual(
                ["Game.kbpack", "Game.night.kbpack", "Game.forest_east.kbpack", "Game.forest_west.kbpack"],
                [path.name for path in pack_set.new_packs],
            )

    def test_patch_release_ships_the_patched_packs_unchanged_and_seals_only_the_patch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            cook = job / "cook"
            stage = root / "stage"
            cook.mkdir(parents=True)
            stage.mkdir()
            pack = cook / "Game.kbpack"
            pack.write_bytes(b"new cook")
            previous = self._previous_release(root, 7, "21kb-pack-set 1\nbase Game.kbpack\nchunk cell Game.cell.kbpack\n")
            kb_cli = root / "kb_cli.exe"
            key = root / "game.kbkey"
            key.write_bytes(b"key")
            args = self._content_args(root, signing_key=key, patch_from=previous, pack_compression_level=0)
            package_game._validate_content_packaging(args)
            self.assertEqual(1, args.patch_level)

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                if argv[1:3] == ["pack", "patch"]:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"patch")
                return mock.MagicMock()

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "emit_diagnostic"), \
                    mock.patch.object(package_game, "run_checked", side_effect=run) as checked:
                pack_set = package_game._build_pack_set(args, Path("cmake.exe"), pack, job)
                package_game._sign_pack(args, Path("cmake.exe"), pack_set, job)
                package_game._stage_pack_set(args, pack, stage, job)

            commands = [[str(value) for value in call.args[0]] for call in checked.call_args_list]
            patch = cook / "Game.patch-0001.kbpack"
            self.assertEqual(
                [str(kb_cli), "pack", "patch", "--current", str(previous / "Game.kbpackset"), "--current-release",
                 str(previous), "--patch-level", "1", "--label", "patch-0001", "--level", "0", "--output", str(patch),
                 str(pack)],
                commands[0],
            )
            self.assertEqual([str(patch)], [command[-3] for command in commands if command[1:3] == ["pack", "sign"]])
            self.assertEqual(
                "21kb-pack-set 1\nbase Game.kbpack\nchunk cell Game.cell.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n",
                (stage / "Game.kbpackset").read_text(encoding="utf-8"),
            )
            self.assertEqual(b"base pack", (stage / "Game.kbpack").read_bytes())
            self.assertEqual((previous / "Game.cell.kbpack").read_bytes(), (stage / "Game.cell.kbpack").read_bytes())
            self.assertEqual(b"patch", (stage / "Game.patch-0001.kbpack").read_bytes())
            self.assertEqual(
                [str(kb_cli), "pack", "set-keys", "--previous-release", str(previous), str(stage / "Game.kbpackset")],
                commands[-2],
            )
            self.assertEqual(["pack", "set-verify"], commands[-1][1:3])

    def test_encrypted_patch_release_wraps_the_keys_of_the_packs_it_ships_again(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            cook = job / "cook"
            stage = root / "stage"
            cook.mkdir(parents=True)
            stage.mkdir()
            pack = cook / "Game.kbpack"
            pack.write_bytes(b"new cook")
            previous = self._previous_release(
                root, 7, "21kb-pack-set 1\nbase Game.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n"
                f"key {'ab' * 72} Game.kbpack\n"
            )
            kb_cli = root / "kb_cli.exe"
            key = root / "game.kbkey"
            key.write_bytes(b"key")
            args = self._content_args(root, signing_key=key, patch_from=previous, encrypt_pack=True)
            package_game._validate_content_packaging(args)
            self.assertEqual(2, args.patch_level)
            self.assertEqual(
                [("base", "", 0, "Game.kbpack"), ("patch", "patch-0001", 1, "Game.patch-0001.kbpack")],
                args.patch_base_entries,
            )

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                if argv[1:3] == ["pack", "patch"]:
                    Path(argv[argv.index("--output") + 1]).write_bytes(b"patch")
                return mock.MagicMock()

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "_build_tool_path", return_value=kb_cli), \
                    mock.patch.object(package_game, "emit_diagnostic"), \
                    mock.patch.object(package_game, "run_checked", side_effect=run) as checked:
                pack_set = package_game._build_pack_set(args, Path("cmake.exe"), pack, job)
                package_game._sign_pack(args, Path("cmake.exe"), pack_set, job)
                package_game._stage_pack_set(args, pack, stage, job)

            commands = [[str(value) for value in call.args[0]] for call in checked.call_args_list]
            content_key = str(job / "pack-content.key")
            patch = cook / "Game.patch-0002.kbpack"
            self.assertIn("--current-release", commands[0])
            self.assertEqual(
                [str(kb_cli), "pack", "sign", "--content-key", content_key, str(patch), "--key", str(key)],
                next(command for command in commands if command[1:3] == ["pack", "sign"]),
            )
            # The new index lists the packs only; set-keys writes the key lines under the new key.
            self.assertEqual(
                "21kb-pack-set 1\nbase Game.kbpack\npatch 1 patch-0001 Game.patch-0001.kbpack\n"
                "patch 2 patch-0002 Game.patch-0002.kbpack\n",
                (stage / "Game.kbpackset").read_text(encoding="utf-8"),
            )
            self.assertEqual(
                [str(kb_cli), "pack", "set-keys", "--content-key", content_key, "--previous-release", str(previous),
                 str(stage / "Game.kbpackset")],
                commands[-2],
            )
            self.assertEqual(
                [str(kb_cli), "pack", "set-verify", "--anchor", str(job / "trust-anchor.bin"), str(stage / "Game.kbpackset")],
                commands[-1],
            )

    def test_single_pack_package_is_staged_without_a_pack_set_index(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            job = root / "job"
            stage = root / "stage"
            job.mkdir()
            stage.mkdir()
            pack = job / "Game.kbpack"
            pack.write_bytes(b"pack")
            args = self._content_args(root)
            package_game._validate_content_packaging(args)
            with mock.patch.object(package_game, "run_checked") as run:
                pack_set = package_game._build_pack_set(args, Path("cmake.exe"), pack, job)
                package_game._stage_pack_set(args, pack, stage, job)
            run.assert_not_called()
            self.assertEqual((pack,), pack_set.new_packs)
            self.assertEqual(["Game.kbpack"], [path.name for path in stage.iterdir()])

    def test_default_product_id_is_a_portable_name(self) -> None:
        self.assertEqual("Acme-Studio.My-Game", package_game._default_product_id("Acme Studio", "My Game!"))
        self.assertEqual("game", package_game._default_product_id("!!", "??"))
        self.assertTrue(package_game._PRODUCT_ID.fullmatch(package_game._default_product_id("x" * 100, "y" * 100)))

    def test_trust_anchor_resource_id_matches_the_engine(self) -> None:
        header = (SCRIPTS.parent / "sources/engine/include/engine/security/ReleaseKeys.hpp").read_text(encoding="utf-8")
        match = re.search(r"kTrustAnchorResourceId = (\d+)U;", header)
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1)), TRUST_ANCHOR_RESOURCE_ID)

    @unittest.skipUnless(sys.platform == "win32", "PE resources are written with the Windows resource API")
    def test_windows_player_carries_the_trust_anchor_resource(self) -> None:
        import ctypes
        from ctypes import wintypes

        with tempfile.TemporaryDirectory() as temporary_text:
            player = Path(temporary_text) / "Player.exe"
            shutil.copy2(sys.executable, player)
            anchor = b"21KBTRST" + bytes(range(90))
            apply_windows_resources(
                player,
                product_name="Game",
                publisher="Publisher",
                version="1.0.0",
                executable_name="Player",
                development=False,
                icon=None,
                trust_anchor=anchor,
            )
            kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
            kernel32.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
            kernel32.LoadLibraryExW.restype = wintypes.HMODULE
            kernel32.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR]
            kernel32.FindResourceW.restype = wintypes.HANDLE
            kernel32.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
            kernel32.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
            kernel32.LoadResource.restype = wintypes.HANDLE
            kernel32.LockResource.argtypes = [wintypes.HANDLE]
            kernel32.LockResource.restype = ctypes.c_void_p
            kernel32.FreeLibrary.argtypes = [wintypes.HMODULE]
            module = kernel32.LoadLibraryExW(str(player), None, 0x00000002 | 0x00000020)
            self.assertTrue(module)
            try:
                resource = kernel32.FindResourceW(
                    module,
                    ctypes.cast(ctypes.c_void_p(TRUST_ANCHOR_RESOURCE_ID), wintypes.LPCWSTR),
                    ctypes.cast(ctypes.c_void_p(10), wintypes.LPCWSTR),
                )
                self.assertTrue(resource)
                size = kernel32.SizeofResource(module, resource)
                data = kernel32.LockResource(kernel32.LoadResource(module, resource))
                self.assertEqual(anchor, ctypes.string_at(data, size))
            finally:
                kernel32.FreeLibrary(module)

    def test_linux_result_archive_rejects_parent_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            archive_path = root / "bad.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                data = b"escape"
                info = tarfile.TarInfo("../escape")
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            destination = root / "result"
            destination.mkdir()
            with self.assertRaisesRegex(PackagingError, "unsafe path"):
                package_game._extract_linux_result(archive_path, destination)
            self.assertFalse((root / "escape").exists())

    def test_remote_linux_release_is_signed_on_the_host_before_the_guest_proves_its_first_frame(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            engine = root / "engine"
            (engine / "scripts").mkdir(parents=True)
            for helper in ("package_linux_guest.py", "elf_trust_anchor.py", "package_contract.py"):
                (engine / "scripts" / helper).write_text("# helper\n", encoding="utf-8")
            job = root / "job"
            (job / "cook").mkdir(parents=True)
            stage = root / "linux-stage"
            stage.mkdir()
            pack = job / "cook" / "Game.kbpack"
            pack.write_bytes(b"base")
            (job / "cook" / "Game.cell.kbpack").write_bytes(b"chunk")
            anchor = job / "trust-anchor.bin"
            anchor.write_bytes(b"anchor")
            kb_cli = root / "kb_cli.exe"
            signing = package_game.ReleaseSigning(kb_cli, root / "game.kbkey", None)
            args = self._content_args(
                root, target="Linux.x64", configuration="Release", engine_root=engine, executable_name="Game",
                engine_fingerprint="f" * 64, linux_host="builder.example", linux_user="packager",
                linux_host_key="ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIExample", linux_port=22,
                linux_engine_root="/srv/21kb", linux_identity=None, trust_anchor=anchor, release_signing=signing,
                version="1.0", release_number=5, anti_rollback=False,
                pack_set=package_game.PackSet(
                    (pack,), ((pack, "Game.kbpack"), (job / "cook" / "Game.cell.kbpack", "Game.cell.kbpack")),
                    "21kb-pack-set 1\nbase Game.kbpack\nchunk cell Game.cell.kbpack\n",
                ),
            )
            events: list[str] = []

            def run(arguments: list[object], **_kwargs: object) -> object:
                argv = [str(value) for value in arguments]
                tool = Path(argv[0]).name
                if tool == "ssh" and "--archive" in argv[-1]:
                    events.append("build")
                    self.assertNotIn("Game.kbpack", argv[-1])
                elif tool == "ssh" and "--prove-archive" in argv[-1]:
                    events.append("prove")
                elif tool == "scp" and argv[-2].endswith("result.tar.gz"):
                    player = root / "player"
                    player.mkdir()
                    (player / "Game").write_bytes(b"\x7fELF\x02\x01\x01" + bytes(11) + (62).to_bytes(2, "little") + b"player")
                    package_game._create_deterministic_tar(player, Path(argv[-1]), executable_name="Game")
                elif tool == "scp" and argv[-2].endswith("linux-build.receipt.json"):
                    Path(argv[-1]).write_bytes(package_game._linux_build_receipt(args, stage))
                elif tool == "scp" and any(value.endswith("linux-release.tar.gz") for value in argv):
                    with tarfile.open(Path(argv[-2]), "r:gz") as archive:
                        names = archive.getnames()
                    self.assertIn("release.kbmanifest", names)
                    self.assertIn("Game.kbpackset", names)
                    self.assertIn("Game.cell.kbpack", names)
                elif argv[1:3] == ["release", "sign"]:
                    events.append("sign")
                    (stage / "release.kbmanifest").write_text("signed\n", encoding="utf-8")
                elif argv[1:3] == ["pack", "set-verify"]:
                    events.append("set-verify")
                return mock.MagicMock()

            # The returned player's anchor slot is read with the ELF helper its own tests cover.
            with mock.patch.object(package_game, "_required_executable", side_effect=lambda name: root / name), \
                    mock.patch.object(package_game, "_stage_licenses") as licenses, \
                    mock.patch.object(package_game, "read_trust_anchor", return_value=b"anchor"), \
                    mock.patch.object(package_game, "emit_diagnostic"), \
                    mock.patch.object(package_game, "run_checked", side_effect=run):
                package_game._stage_linux_remote(args, pack, stage, job)
                package_game._verify_linux_stage(stage, args)

            self.assertEqual(["build", "set-verify", "sign", "prove"], events)
            licenses.assert_called_once_with(args, stage)
            self.assertEqual(
                ["Game", "Game.cell.kbpack", "Game.kbpack", "Game.kbpackset", "linux-build.receipt.json",
                 "release.kbmanifest"],
                sorted(path.name for path in stage.iterdir()),
            )
            receipt = json.loads((stage / "linux-build.receipt.json").read_text(encoding="utf-8"))
            self.assertTrue(receipt["firstFrame"])

            # A signed Linux release must carry its manifest when it comes back.
            (stage / "release.kbmanifest").unlink()
            with self.assertRaisesRegex(PackagingError, "signed release manifest"):
                package_game._verify_linux_stage(stage, args)

    def test_linux_guest_proves_the_first_frame_of_the_release_it_is_given(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            stage = Path(temporary_text) / "release"
            stage.mkdir()
            (stage / "Game").write_bytes(b"\x7fELF player")
            (stage / "Game.kbpack").write_bytes(b"pack")
            (stage / "release.kbmanifest").write_text("signed\n", encoding="utf-8")
            seen: list[list[object]] = []

            def run(arguments: list[object], cwd: Path, timeout: int) -> str:
                seen.append(arguments)
                if arguments[0] == "ldd":
                    return "linux-vdso.so.1\n"
                self.assertTrue((cwd / "release.kbmanifest").is_file())
                return "frames=1 rendered=1 shutdown=clean"

            with mock.patch.object(package_linux_guest, "_run", side_effect=run):
                receipt = json.loads(package_linux_guest._prove_release(stage, "Game", "Release", "e" * 64, "xvfb-run"))
            self.assertEqual("ldd", seen[0][0])
            self.assertEqual("xvfb-run", seen[1][0])
            self.assertEqual(
                {"schema": 1, "configuration": "Release", "engineSha256": "e" * 64,
                 "executableSha256": package_linux_guest._sha256(stage / "Game"),
                 "assetPackSha256": package_linux_guest._sha256(stage / "Game.kbpack"), "firstFrame": True},
                receipt,
            )

            def failing(arguments: list[object], cwd: Path, timeout: int) -> str:
                return "linux-vdso.so.1\n" if arguments[0] == "ldd" else "frames=0"

            with mock.patch.object(package_linux_guest, "_run", side_effect=failing):
                with self.assertRaisesRegex(package_linux_guest.GuestError, "clean first frame"):
                    package_linux_guest._prove_release(stage, "Game", "Release", "e" * 64, "xvfb-run")

    def test_linux_guest_smoke_cannot_add_runtime_artifacts_to_result_archive(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            stage = root / "stage"
            stage.mkdir()
            (stage / "Game").write_bytes(b"\x7fELF")
            (stage / "Game.kbpack").write_bytes(b"pack")

            def run(arguments: list[object], cwd: Path, timeout: int) -> str:
                self.assertEqual(["xvfb-run", "-a", cwd / "Game", "--frames=1"], arguments)
                self.assertEqual(180, timeout)
                artifact = cwd / "Saved/Logs/bgfx-pso-trace.log"
                artifact.parent.mkdir(parents=True)
                artifact.write_text("epoch_ms=1\n", encoding="ascii")
                return "frames=1 rendered=1 shutdown=clean"

            with mock.patch.object(package_linux_guest, "_run", side_effect=run):
                output = package_linux_guest._run_linux_first_frame(stage, "Game", "xvfb-run")

            self.assertEqual("frames=1 rendered=1 shutdown=clean", output)
            self.assertFalse((stage / "Saved").exists())
            archive_path = root / "result.tar.gz"
            package_linux_guest._create_archive(stage, archive_path)
            with tarfile.open(archive_path, "r:gz") as archive:
                self.assertNotIn("Saved/Logs/bgfx-pso-trace.log", archive.getnames())

    def test_linux_runtime_tar_marks_only_the_root_player_executable(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            source = root / "source"
            source.mkdir()
            (source / "Game").write_bytes(b"\x7fELF")
            (source / "data.bin").write_bytes(b"data")
            nested = source / "nested"
            nested.mkdir()
            (nested / "Game").write_bytes(b"nested")
            archive_path = root / "package.tar.gz"

            package_game._create_deterministic_tar(
                source,
                archive_path,
                executable_name="Game",
            )

            with tarfile.open(archive_path, "r:gz") as archive:
                modes = {member.name: member.mode & 0o777 for member in archive.getmembers()}
            self.assertEqual(0o755, modes["Game"])
            self.assertEqual(0, modes["data.bin"] & 0o111)
            self.assertEqual(0, modes["nested/Game"] & 0o111)

    def test_linux_release_archive_requires_root_player_mode_0755(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            archive_path = root / "release.tar.gz"

            def write_archive(player_mode: int) -> None:
                with tarfile.open(archive_path, "w:gz") as archive:
                    for name, data, mode in (
                        ("Game", b"\x7fELF", player_mode),
                        ("Game.kbpack", b"pack", 0o644),
                        ("linux-build.receipt.json", b"{}\n", 0o644),
                    ):
                        info = tarfile.TarInfo(name)
                        info.size = len(data)
                        info.mode = mode
                        archive.addfile(info, io.BytesIO(data))

            write_archive(0o644)
            with self.assertRaisesRegex(PackagingError, "mode 0755"):
                package_game._verify_linux_release_archive(archive_path, "Game")

            write_archive(0o755)
            package_game._verify_linux_release_archive(archive_path, "Game")
            with self.assertRaisesRegex(PackagingError, "signed release manifest"):
                package_game._verify_linux_release_archive(archive_path, "Game", signed=True)

    def test_linux_runtime_rejects_a_non_executable_player_before_hashing(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "Game").write_bytes(b"\x7fELF")
            with mock.patch.object(package_linux_guest.os, "access", return_value=False), \
                    mock.patch.object(package_linux_guest, "_sha256") as sha256:
                with self.assertRaisesRegex(package_linux_guest.GuestError, "not executable"):
                    package_linux_guest._verify_linux_runtime(
                        root,
                        "Game",
                        {"configuration": "Development", "inputs": {"engineSha256": "0" * 64}},
                    )
            sha256.assert_not_called()

    def test_linux_visible_display_preserves_screen_and_uses_authenticated_server(self) -> None:
        command = (
            b"/usr/lib/xorg/Xorg\0:0\0vt7\0-nolisten\0tcp\0-noreset\0-auth\0"
            b"/run/user/1000/Xauthority\0"
        )
        xorg = mock.Mock(returncode=0, stdout="4145\n")

        def lstat(path: Path) -> mock.Mock:
            if path.name == "X0":
                return mock.Mock(st_mode=stat.S_IFSOCK | 0o777)
            if path.name == "Xauthority":
                return mock.Mock(st_mode=stat.S_IFREG | 0o600, st_uid=1000)
            raise FileNotFoundError(path)

        with mock.patch.object(package_linux_guest.subprocess, "run", return_value=xorg), \
                mock.patch.object(Path, "read_bytes", return_value=command), \
                mock.patch.object(Path, "lstat", autospec=True, side_effect=lstat), \
                mock.patch.object(package_linux_guest.os, "getuid", return_value=1000, create=True):
            environment = package_linux_guest._visible_display_environment(":0.0")

        self.assertEqual(":0.0", environment["DISPLAY"])
        self.assertEqual(str(Path("/run/user/1000/Xauthority")), environment["XAUTHORITY"])

    def test_linux_visible_display_rejects_unsafe_xorg_inputs(self) -> None:
        base = [
            b"/usr/lib/xorg/Xorg", b":0", b"vt7", b"-nolisten", b"tcp", b"-noreset",
            b"-auth", b"/run/user/1000/Xauthority",
        ]
        xorg = mock.Mock(returncode=0, stdout="4145\n")

        def reject(
            command: list[bytes],
            *,
            socket_mode: int = stat.S_IFSOCK | 0o777,
            authority_mode: int = stat.S_IFREG | 0o600,
            authority_uid: int = 1000,
        ) -> None:
            def lstat(path: Path) -> mock.Mock:
                if path.name == "X0":
                    return mock.Mock(st_mode=socket_mode)
                return mock.Mock(st_mode=authority_mode, st_uid=authority_uid)

            command_bytes = b"\0".join(command) + b"\0"
            with mock.patch.object(package_linux_guest.subprocess, "run", return_value=xorg), \
                    mock.patch.object(Path, "read_bytes", return_value=command_bytes), \
                    mock.patch.object(Path, "lstat", autospec=True, side_effect=lstat), \
                    mock.patch.object(package_linux_guest.os, "getuid", return_value=1000, create=True):
                with self.assertRaises(package_linux_guest.GuestError):
                    package_linux_guest._visible_display_environment(":0")

        cases = (
            ("regular socket path", base, stat.S_IFREG | 0o600, stat.S_IFREG | 0o600, 1000),
            ("symlink socket path", base, stat.S_IFLNK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("missing auth", base[:-2], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("duplicate auth", base + [b"-auth", b"/tmp/other"], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("empty auth argument", base[:-1] + [b"", base[-1]], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("empty nolisten argument", base[:4] + [b"", *base[4:]], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("access control disabled", base + [b"-ac"], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("relative authority", base[:-1] + [b"relative"], stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 1000),
            ("symlink authority", base, stat.S_IFSOCK | 0o777, stat.S_IFLNK | 0o777, 1000),
            ("wrong authority owner", base, stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o600, 2000),
            ("wrong authority mode", base, stat.S_IFSOCK | 0o777, stat.S_IFREG | 0o644, 1000),
        )
        for name, command, socket_mode, authority_mode, authority_uid in cases:
            with self.subTest(name=name):
                reject(
                    list(command),
                    socket_mode=socket_mode,
                    authority_mode=authority_mode,
                    authority_uid=authority_uid,
                )

    def test_linux_display_failure_does_not_mutate_the_existing_deployment(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            launch_root = root / "runs"
            deployed = launch_root / ("d" * 64)
            deployed.mkdir(parents=True)
            sentinel = deployed / "running.bin"
            sentinel.write_bytes(b"preserved")
            with mock.patch.object(
                    package_linux_guest,
                    "_visible_display_environment",
                    side_effect=package_linux_guest.GuestError("unsafe display"),
            ), mock.patch.object(package_linux_guest.shutil, "copytree") as copytree:
                with self.assertRaisesRegex(package_linux_guest.GuestError, "unsafe display"):
                    package_linux_guest._deploy_and_launch_runtime(
                        root / "runtime",
                        launch_root,
                        "d" * 64,
                        "Game",
                        {},
                        ":0",
                    )
            copytree.assert_not_called()
            self.assertEqual(b"preserved", sentinel.read_bytes())

    def test_linux_launch_accepts_only_an_exact_sealed_unit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text) / "package"
            root.mkdir()
            (root / "player").write_bytes(b"payload")
            seal_with_first_frame(root, {
                "target": "Linux.x64",
                "configuration": "Development",
                "inputs": {"engineSha256": "0" * 64},
            })
            receipt = package_linux_guest._verify_package_unit(root)
            self.assertEqual("Linux.x64", receipt["target"])
            receipt_path = root / "package.receipt.json"
            receipt["runtimeFirstFrame"]["observed"] = "changed"
            receipt_path.write_bytes(package_contract.canonical_json_bytes(receipt))
            with self.assertRaisesRegex(package_linux_guest.GuestError, "invalid runtime first-frame evidence"):
                package_linux_guest._verify_package_unit(root)
            receipt["runtimeFirstFrame"]["observed"] = "frames=1 rendered=1 shutdown=clean"
            receipt_path.write_bytes(package_contract.canonical_json_bytes(receipt))
            (root / "unexpected").write_bytes(b"extra")
            with self.assertRaisesRegex(package_linux_guest.GuestError, "file set"):
                package_linux_guest._verify_package_unit(root)

    def test_linux_launch_stops_process_when_pid_cannot_be_recorded(self) -> None:
        class RunningProcess:
            pid = 4312
            returncode = None

            def __init__(self) -> None:
                self.terminated = False
                self.waited = False

            def poll(self) -> int | None:
                return None

            def terminate(self) -> None:
                self.terminated = True

            def wait(self, timeout: float | None = None) -> int:
                del timeout
                self.waited = True
                return 0

        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            archive = root / "launch.tar.gz"
            archive.write_bytes(b"archive")
            args = argparse.Namespace(
                launch_archive=archive,
                executable_name="Game",
                display=":1",
            )
            receipt = {
                "configuration": "Development",
                "manifestSha256": "a" * 64,
            }
            process = RunningProcess()
            completed = mock.Mock(returncode=0, stdout="frames=1 rendered=1 shutdown=clean")
            environment = {"DISPLAY": ":1", "XAUTHORITY": "/authority"}
            with mock.patch.object(Path, "home", return_value=root / "home"), \
                    mock.patch.object(package_linux_guest, "_extract_input"), \
                    mock.patch.object(package_linux_guest, "_verify_package_unit", return_value=receipt), \
                    mock.patch.object(package_linux_guest, "_verify_linux_runtime", return_value=Path("Game")), \
                    mock.patch.object(package_linux_guest, "_visible_display_environment", return_value=environment), \
                    mock.patch.object(package_linux_guest.subprocess, "run", return_value=completed), \
                    mock.patch.object(package_linux_guest.subprocess, "Popen", return_value=process), \
                    mock.patch.object(package_linux_guest.time, "sleep"), \
                    mock.patch.object(package_linux_guest, "_GuestFileLock", return_value=mock.MagicMock()), \
                    mock.patch.object(package_linux_guest, "_write_pid_atomically", side_effect=OSError("disk full")):
                with self.assertRaisesRegex(OSError, "disk full"):
                    package_linux_guest._launch_package(args)

            self.assertTrue(process.terminated and process.waited)

    def test_repeat_linux_launch_preserves_a_live_deployment_and_pid(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            archive = root / "launch.tar.gz"
            archive.write_bytes(b"archive")
            identity = "b" * 64
            launch_root = root / "home" / ".local" / "state" / "21kb" / "package-runs"
            deployed = launch_root / identity
            deployed.mkdir(parents=True)
            sentinel = deployed / "running.bin"
            sentinel.write_bytes(b"owned-by-running-process")
            pid_file = launch_root / f"{identity}.pid"
            pid_file.write_text("4312\n", encoding="ascii")
            args = argparse.Namespace(
                launch_archive=archive,
                executable_name="Game",
                display=":1",
            )
            receipt = {
                "configuration": "Development",
                "manifestSha256": identity,
            }
            with mock.patch.object(Path, "home", return_value=root / "home"), \
                    mock.patch.object(package_linux_guest, "_extract_input"), \
                    mock.patch.object(package_linux_guest, "_verify_package_unit", return_value=receipt), \
                    mock.patch.object(package_linux_guest, "_verify_linux_runtime", return_value=Path("Game")), \
                    mock.patch.object(package_linux_guest, "_visible_display_environment", return_value={}), \
                    mock.patch.object(package_linux_guest.os, "kill") as signal_probe, \
                    mock.patch.object(package_linux_guest, "_GuestFileLock", return_value=mock.MagicMock()), \
                    mock.patch.object(package_linux_guest.subprocess, "Popen") as popen:
                with self.assertRaisesRegex(package_linux_guest.GuestError, "already running"):
                    package_linux_guest._launch_package(args)

            signal_probe.assert_called_once_with(4312, 0)
            popen.assert_not_called()
            self.assertEqual(b"owned-by-running-process", sentinel.read_bytes())
            self.assertEqual("4312\n", pid_file.read_text(encoding="ascii"))

    def test_linux_launch_lock_covers_probe_deploy_start_and_pid_publish(self) -> None:
        class LockProbe:
            active = False

            def __enter__(self) -> "LockProbe":
                self.active = True
                return self

            def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
                self.active = False

        class RunningProcess:
            pid = 9876
            returncode = None

            @staticmethod
            def poll() -> None:
                return None

        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            archive = root / "launch.tar.gz"
            archive.write_bytes(b"archive")
            receipt = {"configuration": "Development", "manifestSha256": "c" * 64}
            args = argparse.Namespace(launch_archive=archive, executable_name="Game", display=":1")
            completed = mock.Mock(returncode=0, stdout="frames=1 rendered=1 shutdown=clean")
            lock = LockProbe()
            original_copytree = package_linux_guest.shutil.copytree
            events: list[str] = []
            environment = {"DISPLAY": ":1", "XAUTHORITY": "/authority"}

            def display_environment(value: str) -> dict[str, str]:
                self.assertEqual(":1", value)
                events.append("display")
                return environment

            def probe(_path: Path) -> bool:
                self.assertTrue(lock.active)
                events.append("probe")
                return False

            def copytree(source: Path, destination: Path, **kwargs: object) -> Path:
                self.assertTrue(lock.active)
                events.append("deploy")
                return original_copytree(source, destination, **kwargs)

            def publish_pid(_path: Path, pid: int) -> None:
                self.assertTrue(lock.active)
                self.assertEqual(9876, pid)
                events.append("pid")

            def smoke(*_args: object, **kwargs: object) -> mock.Mock:
                self.assertIs(environment, kwargs["env"])
                return completed

            def start(*_args: object, **kwargs: object) -> RunningProcess:
                self.assertTrue(lock.active)
                self.assertIs(environment, kwargs["env"])
                events.append("start")
                return RunningProcess()

            with mock.patch.object(Path, "home", return_value=root / "home"), \
                    mock.patch.object(package_linux_guest, "_extract_input"), \
                    mock.patch.object(package_linux_guest, "_verify_package_unit", return_value=receipt), \
                    mock.patch.object(package_linux_guest, "_verify_linux_runtime", return_value=Path("Game")), \
                    mock.patch.object(package_linux_guest, "_visible_display_environment", side_effect=display_environment) as display_probe, \
                    mock.patch.object(package_linux_guest, "_GuestFileLock", return_value=lock), \
                    mock.patch.object(package_linux_guest, "_recorded_launch_is_alive", side_effect=probe), \
                    mock.patch.object(package_linux_guest.shutil, "copytree", side_effect=copytree), \
                    mock.patch.object(package_linux_guest.subprocess, "run", side_effect=smoke), \
                    mock.patch.object(package_linux_guest.subprocess, "Popen", side_effect=start), \
                    mock.patch.object(package_linux_guest.time, "sleep"), \
                    mock.patch.object(package_linux_guest, "_write_pid_atomically", side_effect=publish_pid):
                package_linux_guest._launch_package(args)

            display_probe.assert_called_once_with(":1")
            self.assertEqual(["display", "probe", "deploy", "start", "pid"], events)

    def test_remote_linux_launch_uploads_the_shared_fingerprint_contract(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            output = root / "published"
            output.mkdir()
            (output / "Game").write_bytes(b"\x7fELF")
            seal_with_first_frame(output, {
                "target": "Linux.x64",
                "configuration": "Development",
                "inputs": {"engineSha256": "0" * 64},
            })
            scripts = root / "engine" / "scripts"
            scripts.mkdir(parents=True)
            helper = scripts / "package_linux_guest.py"
            contract = scripts / "package_contract.py"
            helper.write_text("helper", encoding="ascii")
            contract.write_text("contract", encoding="ascii")
            args = argparse.Namespace(
                output=output,
                configuration="Development",
                engine_root=root / "engine",
                build_root=root / "build",
                executable_name="Game",
                linux_host="builder.example",
                linux_user="builder",
                linux_host_key="ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest",
                linux_port=22,
                linux_display=":1",
                linux_identity=None,
            )
            with mock.patch.object(package_game.sys, "platform", "win32"), \
                    mock.patch.object(package_game, "_required_executable", side_effect=lambda name: Path(name)), \
                    mock.patch.object(package_game, "run_checked") as run:
                package_game._launch_linux(args)

            scp_arguments = run.call_args_list[1].args[0]
            self.assertIn(helper, scp_arguments)
            self.assertIn(contract, scp_arguments)

    def test_local_linux_configure_uses_dedicated_package_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = argparse.Namespace(
                configuration="Development",
                target="Linux.x64",
                build_root=root / "build",
                engine_root=root / "engine",
            )
            captured: list[object] = []

            def stop_after_configure(arguments: object, **_kwargs: object) -> None:
                captured.extend(arguments)  # type: ignore[arg-type]
                raise PackagingError("stop")

            with mock.patch.object(package_game.sys, "platform", "linux"), \
                    mock.patch.object(package_game, "run_checked", side_effect=stop_after_configure):
                with self.assertRaisesRegex(PackagingError, "stop"):
                    package_game._stage_linux_local(args, Path("cmake"), root / "pack", root / "stage")
            build_argument = captured[captured.index("-B") + 1]
            self.assertEqual(args.build_root / "packages" / "Linux.x64" / "Debug", build_argument)
            self.assertNotEqual(args.build_root, build_argument)

    def test_linux_guest_locks_build_and_rejects_source_change(self) -> None:
        class LockProbe:
            entered = False
            exited = False

            def __init__(self, _path: Path) -> None:
                pass

            def __enter__(self) -> "LockProbe":
                self.entered = True
                return self

            def __exit__(self, _type: object, _value: object, _traceback: object) -> None:
                self.exited = True

        with tempfile.TemporaryDirectory() as temporary_text:
            engine = Path(temporary_text) / "engine"
            engine.mkdir()
            lock = LockProbe(Path("unused"))
            with mock.patch.object(package_linux_guest, "_GuestFileLock", return_value=lock), \
                    mock.patch.object(package_linux_guest, "_engine_hash", side_effect=["expected", "changed"]), \
                    mock.patch.object(package_linux_guest, "_run") as run:
                with self.assertRaisesRegex(package_linux_guest.GuestError, "changed during"):
                    package_linux_guest._build_linux_player(engine, "Debug", "expected", "cmake")
            self.assertTrue(lock.entered and lock.exited)
            self.assertEqual(2, run.call_count)

    def test_release_android_allows_the_editor_signing_broker(self) -> None:
        parsed = package_game._parse_arguments([
            "--project", "missing",
            "--target", "Android.ETC2.arm64",
            "--configuration", "Release",
            "--output", "out",
            "--engine-root", ".",
            "--build-root", "build",
            "--product-name", "Game",
            "--publisher", "Studio",
            "--version", "1.0",
            "--executable-name", "Game",
        ])
        self.assertEqual("Android.ETC2.arm64", parsed.target)
        self.assertIsNone(parsed.android_signing_broker)

    def test_android_first_frame_failure_force_stops_the_started_application(self) -> None:
        args = argparse.Namespace(
            target="Android.ETC2.arm64",
            android_application_id="com.example.game",
        )
        adb = Path("adb.exe")
        apk = Path("stage/Game-etc2.apk")

        def run(arguments: list[object], **_kwargs: object) -> mock.Mock:
            output = "FATAL EXCEPTION: main" if "-d" in arguments else ""
            return mock.Mock(output=output)

        with mock.patch.object(package_game, "run_checked", side_effect=run) as checked:
            with self.assertRaisesRegex(PackagingError, "failed before its first frame"):
                package_game._verify_android_first_frame(args, apk, adb)

        force_stops = [
            call for call in checked.call_args_list
            if call.args[0][1:4] == ["shell", "am", "force-stop"]
        ]
        self.assertEqual(2, len(force_stops))

    def test_android_stage_uses_adb_only_for_explicit_launch_and_probes_final_apk(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            engine = root / "engine"
            wrapper = engine / "platform/android/gradle/wrapper/gradle-wrapper.jar"
            wrapper.parent.mkdir(parents=True)
            wrapper.write_bytes(b"gradle")
            source_apk = engine / "platform/android/app/build/outputs/apk/etc2/debug/Game.apk"
            source_apk.parent.mkdir(parents=True)
            source_apk.write_bytes(b"final-apk")
            build_tools = root / "sdk/build-tools/35.0.0"
            (build_tools / "lib").mkdir(parents=True)
            (build_tools / "lib/apksigner.jar").write_bytes(b"signer")
            zipalign = build_tools / "zipalign.exe"
            zipalign.write_bytes(b"tool")
            pack = root / "Game.kbpack"
            pack.write_bytes(b"pack")
            adb = root / "adb.exe"
            adb.write_bytes(b"tool")
            args = argparse.Namespace(
                target="Android.ETC2.arm64",
                configuration="Development",
                engine_root=engine,
                build_root=root / "build",
                executable_name="Game",
                product_name="Game",
                version="1.0",
                application_icon=None,
                android_application_id="com.example.game",
                android_version_code=1,
                android_label=None,
                android_min_sdk=28,
                android_target_sdk=35,
                launch=False,
            )

            def stage(launch: bool) -> tuple[package_game.StageResult, Path, mock.Mock, mock.Mock]:
                args.launch = launch
                destination = root / ("stage-launch" if launch else "stage-build")
                destination.mkdir()
                job = root / ("job-launch" if launch else "job-build")
                job.mkdir()
                with mock.patch.object(package_game, "_android_sdk", return_value=root / "sdk"), \
                        mock.patch.object(package_game, "_latest_android_build_tools", return_value=build_tools), \
                        mock.patch.object(package_game, "_build_tool_path", return_value=Path("validator.exe")), \
                        mock.patch.object(package_game, "_stage_licenses"), \
                        mock.patch.object(package_game, "_java", return_value=Path("java.exe")), \
                        mock.patch.object(package_game, "run_checked"), \
                        mock.patch.object(package_game, "_verify_android_apk"), \
                        mock.patch.object(package_game, "_android_adb", return_value=adb) as resolve_adb, \
                        mock.patch.object(package_game, "_verify_android_first_frame") as probe, \
                        mock.patch.object(package_game, "_try_android_force_stop") as force_stop:
                    result = package_game._stage_android(args, pack, destination, job)
                self.assertEqual(b"final-apk", (destination / "Game-etc2.apk").read_bytes())
                force_stop.assert_not_called()
                return result, destination, resolve_adb, probe

            build_result, _, build_adb, build_probe = stage(False)
            self.assertIsNone(build_result.first_frame)
            self.assertIsNone(build_result.running_android_adb)
            build_adb.assert_not_called()
            build_probe.assert_not_called()

            launch_result, launch_stage, launch_adb, launch_probe = stage(True)
            launch_adb.assert_called_once_with()
            launch_probe.assert_called_once_with(args, launch_stage / "Game-etc2.apk", adb)
            self.assertEqual(adb, launch_result.running_android_adb)
            self.assertEqual("android-logcat", launch_result.first_frame.probe)
            self.assertEqual(
                package_contract.payload_manifest_sha256(launch_stage),
                launch_result.first_frame.payload_manifest_sha256,
            )

    def test_android_package_failure_after_probe_force_stops_runtime(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = argparse.Namespace(
                target="Android.ASTC.arm64",
                configuration="Development",
                build_root=root / "build",
                output=root / "package",
                engine_root=root,
                project=root / "Project.21kbproject",
                launch=True,
                android_application_id="com.example.game",
            )
            adb = Path("adb.exe")
            first_frame = package_game.FirstFrameResult(
                "android-logcat",
                "profile=Android.ASTC.arm64 first-frame=rendered",
                "a" * 64,
            )
            stage_result = package_game.StageResult((), first_frame, adb)
            lock = mock.MagicMock()
            with mock.patch.object(package_game, "_validate_arguments"), \
                    mock.patch.object(package_game, "_required_executable", return_value=Path("cmake.exe")), \
                    mock.patch.object(package_game, "FileLock", return_value=lock), \
                    mock.patch.object(package_game, "_project_source_fingerprint", return_value="p" * 64), \
                    mock.patch.object(package_game, "_copy_project_snapshot", return_value=Path("snapshot")), \
                    mock.patch.object(package_game, "_engine_fingerprint", return_value="e" * 64), \
                    mock.patch.object(package_game, "_ensure_host_tools", return_value=(Path("cooker"), Path("validator"))), \
                    mock.patch.object(package_game, "_cook", return_value=Path("Game.kbpack")), \
                    mock.patch.object(package_game, "_sign_pack"), \
                    mock.patch.object(package_game, "_stage_target", return_value=stage_result), \
                    mock.patch.object(package_game, "_receipt", return_value={"target": args.target}), \
                    mock.patch.object(package_game, "seal_unit", side_effect=PackagingError("seal failed")), \
                    mock.patch.object(package_game, "_try_android_force_stop") as force_stop:
                with self.assertRaisesRegex(PackagingError, "seal failed"):
                    package_game.package(args)

            force_stop.assert_called_once_with(adb, args.android_application_id, root)
            self.assertFalse(any((args.build_root / "package-jobs").iterdir()))

    def test_successful_android_package_transfers_runtime_without_second_launch(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = argparse.Namespace(
                target="Android.ASTC.arm64",
                configuration="Development",
                build_root=root / "build",
                output=root / "package",
                engine_root=root,
                project=root / "Project.21kbproject",
                launch=True,
                android_application_id="com.example.game",
            )
            adb = Path("adb.exe")
            first_frame = package_game.FirstFrameResult(
                "android-logcat",
                "profile=Android.ASTC.arm64 first-frame=rendered",
                "a" * 64,
            )
            stage_result = package_game.StageResult((), first_frame, adb)

            def publish(_candidate: Path, output: Path) -> None:
                output.mkdir()

            with mock.patch.object(package_game, "_validate_arguments"), \
                    mock.patch.object(package_game, "_required_executable", return_value=Path("cmake.exe")), \
                    mock.patch.object(package_game, "FileLock", return_value=mock.MagicMock()), \
                    mock.patch.object(package_game, "_project_source_fingerprint", return_value="p" * 64), \
                    mock.patch.object(package_game, "_copy_project_snapshot", return_value=Path("snapshot")), \
                    mock.patch.object(package_game, "_engine_fingerprint", return_value="e" * 64), \
                    mock.patch.object(package_game, "_ensure_host_tools", return_value=(Path("cooker"), Path("validator"))), \
                    mock.patch.object(package_game, "_cook", return_value=Path("Game.kbpack")), \
                    mock.patch.object(package_game, "_sign_pack"), \
                    mock.patch.object(package_game, "_stage_target", return_value=stage_result), \
                    mock.patch.object(package_game, "_receipt", return_value={"target": args.target}), \
                    mock.patch.object(package_game, "seal_unit"), \
                    mock.patch.object(package_game, "verify_unit"), \
                    mock.patch.object(package_game, "atomic_publish", side_effect=publish), \
                    mock.patch.object(package_game, "_launch") as launch, \
                    mock.patch.object(package_game, "_try_android_force_stop") as force_stop:
                package_game.package(args)

            launch.assert_not_called()
            force_stop.assert_not_called()

    def test_application_icon_must_be_project_png(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            project = root / "project" / "Project.21kbproject"
            project.parent.mkdir()
            project.write_bytes(b"project")
            icon = project.parent / "Branding" / "ApplicationIcon.png"
            icon.parent.mkdir()
            icon.write_bytes(b"\x89PNG\r\n\x1a\npayload")
            self.assertEqual(icon.resolve(), package_game._project_png_icon(project, Path("Branding/ApplicationIcon.png")))

            outside = root / "outside.png"
            outside.write_bytes(b"\x89PNG\r\n\x1a\npayload")
            with self.assertRaisesRegex(PackagingError, "inside the project"):
                package_game._project_png_icon(project, outside)
            icon.write_bytes(b"not-png")
            with self.assertRaisesRegex(PackagingError, "readable PNG"):
                package_game._project_png_icon(project, icon)

    def test_package_work_directories_must_be_outside_project(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            project = root / "project" / "Project.21kbproject"
            project.parent.mkdir()
            project.write_bytes(b"project")
            package_game._validate_package_work_roots(project, root / "build", root / "output")
            with self.assertRaisesRegex(PackagingError, "build directory"):
                package_game._validate_package_work_roots(project, project.parent / "CustomBuild", root / "output")
            with self.assertRaisesRegex(PackagingError, "package output"):
                package_game._validate_package_work_roots(project, root / "build", project.parent / "CustomOutput")

    def test_windows_version_resource_uses_all_selected_metadata(self) -> None:
        self.assertEqual((1, 2, 3, 0), _version_tuple("1.2.3-beta.1"))
        resource = _version_resource(
            product_name="Selected Product",
            publisher="Selected Publisher",
            version="1.2.3",
            executable_name="SelectedGame",
            development=False,
        )
        self.assertIn("Selected Product".encode("utf-16le"), resource)
        self.assertIn("Selected Publisher".encode("utf-16le"), resource)
        self.assertIn("SelectedGame.exe".encode("utf-16le"), resource)

    def test_pe_debug_identity_names_the_symbol_store_directory(self) -> None:
        guid = bytes(range(16))
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            write_pe_with_pdb_reference(root / "Game.exe", "kb_game.pdb", guid, 3)
            identity = windows_pe_symbols.pe_pdb_identity(root / "Game.exe")
            assert identity is not None
            self.assertEqual("kb_game.pdb", identity.name)
            self.assertEqual("C:\\Build\\bin\\kb_game.pdb", identity.linked_path)
            # GUID fields in their printed byte order, upper case, then the age in hex.
            self.assertEqual("03020100050407060809" + "0A0B0C0D0E0F" + "3", identity.identifier)
            write_pe_with_pdb_reference(root / "Plain.dll", None, guid, 1)
            self.assertIsNone(windows_pe_symbols.pe_pdb_identity(root / "Plain.dll"))
            write_pdb(root / "kb_game.pdb", guid)
            self.assertEqual(guid, windows_pe_symbols.pdb_guid(root / "kb_game.pdb"))
            (root / "text.pdb").write_bytes(b"not a program database")
            self.assertIsNone(windows_pe_symbols.pdb_guid(root / "text.pdb"))

    def test_symbol_store_uses_the_server_layout_outside_the_package(self) -> None:
        guid = bytes(range(16, 32))
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            built = root / "build" / "bin"
            built.mkdir(parents=True)
            stage = root / "stage"
            (stage / "RuntimeModules").mkdir(parents=True)
            job = root / "job"
            job.mkdir()
            write_pe_with_pdb_reference(built / "kb_game.exe", "kb_game.pdb", guid, 1)
            write_pdb(built / "kb_game.pdb", guid)
            write_pe_with_pdb_reference(stage / "Game.exe", "kb_game.pdb", guid, 1)
            module_guid = bytes(range(32, 48))
            write_pe_with_pdb_reference(stage / "RuntimeModules" / "Gameplay.dll", "Gameplay.pdb", module_guid, 2)
            write_pdb(stage / "RuntimeModules" / "Gameplay.pdb", module_guid)

            symbols = package_game._collect_windows_symbols(
                stage, job, {stage / "Game.exe": built / "kb_game.exe"}, {stage / "Game.exe"}
            )
            self.assertEqual([], [path for path in stage.rglob("*.pdb")], "a PDB was left in the shipped folder")
            self.assertEqual(["kb_game.pdb", "Gameplay.pdb"], sorted((identity.name for identity, _ in symbols), reverse=True))

            store = root / "Game.symbols"
            stored = windows_pe_symbols.publish_to_symbol_store(store, symbols)
            game_entry = store / "kb_game.pdb" / "131211101514171618191A1B1C1D1E1F1" / "kb_game.pdb"
            self.assertIn(game_entry, stored)
            self.assertEqual(guid, windows_pe_symbols.pdb_guid(game_entry))
            self.assertTrue((store / "Gameplay.pdb" / "2322212025242726" "28292A2B2C2D2E2F2" / "Gameplay.pdb").is_file())
            # Publishing the same build again is a no-op rather than a failure.
            self.assertEqual(stored, windows_pe_symbols.publish_to_symbol_store(store, symbols))

            with self.assertRaisesRegex(PackagingError, "symbol store must be outside the package output"):
                project = root / "Project" / "Game.21kbproject"
                project.parent.mkdir()
                project.write_bytes(b"project")
                package_game._validate_package_work_roots(project, root / "build", root / "output", root / "output" / "pdb")

    def test_required_image_needs_the_pdb_of_its_exact_build(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "stage").mkdir()
            (root / "job").mkdir()
            write_pe_with_pdb_reference(root / "kb_game.exe", "kb_game.pdb", bytes(range(16)), 1)
            write_pdb(root / "kb_game.pdb", bytes(range(1, 17)))
            write_pe_with_pdb_reference(root / "stage" / "Game.exe", "kb_game.pdb", bytes(range(16)), 1)
            built = {root / "stage" / "Game.exe": root / "kb_game.exe"}
            with self.assertRaisesRegex(PackagingError, "PDB of this exact build is missing"):
                package_game._collect_windows_symbols(root / "stage", root / "job", built, {root / "stage" / "Game.exe"})
            write_pe_with_pdb_reference(root / "stage" / "Game.exe", None, b"", 0)
            with self.assertRaisesRegex(PackagingError, "linked without a PDB"):
                package_game._collect_windows_symbols(root / "stage", root / "job", built, {root / "stage" / "Game.exe"})

    def test_crash_report_endpoint_is_https_or_loopback_and_written_for_the_player(self) -> None:
        for allowed in ("https://crash.example.com/api/1/minidump/?key=abc", "http://127.0.0.1:8080/submit", "http://[::1]:9/x"):
            self.assertEqual(allowed, package_game._validate_crash_report_url(allowed))
        for refused in ("http://crash.example.com/submit", "http://localhost/submit", "ftp://crash.example.com/",
                        "https://user:secret@crash.example.com/", "https://crash.example.com/a b", "", "https://"):
            with self.assertRaises(PackagingError):
                package_game._validate_crash_report_url(refused)
        with tempfile.TemporaryDirectory() as temporary_text:
            stage = Path(temporary_text)
            package_game._write_crash_report_config(stage, None)
            self.assertFalse((stage / "CrashReports.ini").exists(), "a package without an endpoint must not offer upload")
            package_game._write_crash_report_config(stage, "https://crash.example.com/submit")
            self.assertEqual(
                b"[CrashReports]\nUploadUrl=https://crash.example.com/submit\n",
                (stage / "CrashReports.ini").read_bytes(),
            )

    def test_windows_package_ships_the_crash_report_privacy_notice(self) -> None:
        engine_root = SCRIPTS.parent
        with tempfile.TemporaryDirectory() as temporary_text:
            stage = Path(temporary_text) / "stage"
            stage.mkdir()
            package_game._stage_crash_report_notice(engine_root, stage)
            notice = (stage / "CRASH_REPORTS.txt").read_text(encoding="utf-8")
            self.assertIn("off until you answer yes", notice)
            self.assertEqual((engine_root / "platform" / "windows" / "CRASH_REPORTS.txt").read_bytes(),
                             (stage / "CRASH_REPORTS.txt").read_bytes())
            with self.assertRaisesRegex(PackagingError, "privacy notice is missing"):
                package_game._stage_crash_report_notice(Path(temporary_text), stage)

    @staticmethod
    def _signing_arguments(**overrides: object) -> argparse.Namespace:
        values: dict[str, object] = {
            "target": "Windows.x64",
            "windows_sign_thumbprint": None,
            "windows_sign_pfx": None,
            "windows_sign_password_stdin": False,
            "windows_signing_broker": None,
            "windows_timestamp_url": None,
            "windows_require_signing": False,
        }
        values.update(overrides)
        return argparse.Namespace(**values)

    def test_windows_signing_options_are_validated(self) -> None:
        package_game._validate_windows_signing(self._signing_arguments())
        with self.assertRaisesRegex(PackagingError, "required but no certificate"):
            package_game._validate_windows_signing(self._signing_arguments(windows_require_signing=True))
        store = self._signing_arguments(windows_sign_thumbprint="ab:cd:ef:01:23:45:67:89:ab:cd:ef:01:23:45:67:89:ab:cd:ef:01",
                                        windows_timestamp_url="http://timestamp.example.com/rfc3161")
        package_game._validate_windows_signing(store)
        self.assertEqual("ABCDEF0123456789ABCDEF0123456789ABCDEF01", store.windows_sign_thumbprint)
        with tempfile.TemporaryDirectory() as temporary_text:
            pfx = Path(temporary_text) / "release.pfx"
            pfx.write_bytes(b"pfx")
            for refused, message in (
                ({"windows_sign_thumbprint": "1234"}, "40 hexadecimal"),
                ({"windows_sign_thumbprint": "A" * 40, "windows_sign_pfx": pfx}, "not both"),
                ({"windows_sign_thumbprint": "A" * 40, "windows_sign_password_stdin": True}, "needs no password"),
                ({"windows_sign_pfx": pfx, "windows_sign_password_stdin": True, "windows_signing_broker": pfx}, "not both"),
                ({"windows_sign_pfx": pfx, "windows_timestamp_url": "ftp://timestamp.example.com"}, "RFC 3161"),
                ({"windows_sign_pfx": pfx, "target": "Linux.x64"}, "only to Windows"),
                ({"windows_timestamp_url": "http://timestamp.example.com"}, "need a certificate"),
            ):
                with self.assertRaisesRegex(PackagingError, message):
                    package_game._validate_windows_signing(self._signing_arguments(**refused))
            with self.assertRaises(OSError):
                package_game._validate_windows_signing(self._signing_arguments(windows_sign_pfx=Path(temporary_text) / "missing.pfx"))

    def test_store_signing_names_the_certificate_and_never_a_secret(self) -> None:
        command = windows_authenticode.signtool_sign_command(
            Path("signtool.exe"), [Path("Game.exe"), Path("plugin.dll")],
            "abcdef0123456789abcdef0123456789abcdef01", "http://timestamp.example.com/rfc3161")
        self.assertEqual(
            ["sign", "/fd", "SHA256", "/s", "My", "/sha1", "ABCDEF0123456789ABCDEF0123456789ABCDEF01",
             "/tr", "http://timestamp.example.com/rfc3161", "/td", "SHA256"],
            [str(part) for part in command[1:12]],
        )
        self.assertNotIn("/p", [str(part) for part in command])
        self.assertNotIn("/f", [str(part) for part in command])
        if os.name == "nt" and (Path(os.environ.get("ProgramFiles(x86)", "")) / "Windows Kits").is_dir():
            signtool = windows_authenticode.find_signtool()
            self.assertEqual("signtool.exe", signtool.name.lower())
            self.assertEqual("x64", signtool.parent.name.lower())

    @unittest.skipUnless(os.name == "nt" and built_authenticode_signer() is not None,
                         "needs Windows and a built kb_authenticode_signer")
    def test_pfx_signing_is_verified_against_the_certificate(self) -> None:
        signer = built_authenticode_signer()
        assert signer is not None
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            password = secrets.token_urlsafe(18)
            pfx, thumbprint = create_throwaway_certificate(root, password)
            images = [root / "Game.exe", root / "plugin.dll"]
            for image in images:
                shutil.copy2(signer, image)
            unsigned = root / "Unsigned.exe"
            shutil.copy2(signer, unsigned)
            reported = package_game._sign_with_pfx_signer(signer, pfx, password, images, None, root)
            self.assertEqual(thumbprint, reported)
            signatures = windows_authenticode.verify_signed(images, expected_thumbprint=thumbprint, allow_untrusted_root=True)
            self.assertEqual(["21kb Packaging Test"] * 2, [signature.subject for signature in signatures])
            # A throwaway root is not trusted, which a release must refuse.
            with self.assertRaisesRegex(windows_authenticode.AuthenticodeError, "does not trust"):
                windows_authenticode.verify_signed(images[:1], expected_thumbprint=thumbprint)
            with self.assertRaisesRegex(windows_authenticode.AuthenticodeError, "not signed"):
                windows_authenticode.verify_signed([unsigned], expected_thumbprint=thumbprint, allow_untrusted_root=True)
            with self.assertRaisesRegex(windows_authenticode.AuthenticodeError, "not by"):
                windows_authenticode.verify_signed(images[:1], expected_thumbprint="0" * 40, allow_untrusted_root=True)
            tampered = bytearray(images[0].read_bytes())
            tampered[len(tampered) // 3] ^= 0xFF
            images[0].write_bytes(bytes(tampered))
            with self.assertRaisesRegex(windows_authenticode.AuthenticodeError, "invalid signature"):
                windows_authenticode.verify_signed(images[:1], expected_thumbprint=thumbprint, allow_untrusted_root=True)
            with self.assertRaises(PackagingError):
                package_game._sign_with_pfx_signer(signer, pfx, "wrong-" + password, [unsigned], None, root)

    def test_windows_images_are_signed_after_every_binary_edit(self) -> None:
        plugins = ("kb_physics_jolt_plugin", "kb_audio_miniaudio_plugin", "kb_basic_lighting_plugin", "kb_21kb_particle_plugin")
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            built = root / "build" / "bin"
            built.mkdir(parents=True)
            for index, name in enumerate(("kb_game.exe", *(f"{plugin}.dll" for plugin in plugins))):
                guid = bytes([index] * 16)
                stem = Path(name).stem
                write_pe_with_pdb_reference(built / name, f"{stem}.pdb", guid, 1)
                write_pdb(built / f"{stem}.pdb", guid)
            pack = root / "cook" / "Game.kbpack"
            pack.parent.mkdir()
            pack.write_bytes(b"pack")
            stage = root / "stage"
            stage.mkdir()
            job = root / "job"
            job.mkdir()
            args = self._signing_arguments(
                configuration="Release", build_root=root / "build", engine_root=SCRIPTS.parent, executable_name="Game",
                product_name="Game", publisher="Publisher", version="1.0.0", application_icon=None, crash_report_url=None,
                windows_sign_thumbprint="A" * 40, windows_timestamp_url="http://timestamp.example.com/rfc3161",
            )
            events: list[str] = []
            signed: list[Path] = []

            def run(arguments: list[object], **_: object) -> package_contract.ProcessResult:
                if str(arguments[1]) == "sign":
                    events.append("sign")
                    signed.extend(Path(str(argument)) for argument in arguments[12:])
                    return package_contract.ProcessResult("", 0.0)
                events.append("first frame")
                return package_contract.ProcessResult("frames=1 shutdown=clean rendered=1", 0.0)

            with mock.patch.object(package_game, "_build_targets"), \
                    mock.patch.object(package_game, "apply_windows_resources", side_effect=lambda *_, **__: events.append("resources")), \
                    mock.patch.object(package_game, "find_signtool", return_value=Path("signtool.exe")), \
                    mock.patch.object(package_game, "verify_signed", side_effect=lambda *_, **__: events.append("verify")), \
                    mock.patch.object(package_game, "run_checked", side_effect=run):
                result = package_game._stage_windows(args, Path("cmake"), pack, stage, job)
            self.assertEqual(["resources", "sign", "verify", "first frame"], events)
            self.assertEqual(sorted(path.name for path in stage.rglob("*") if path.suffix in (".exe", ".dll")),
                             sorted(path.name for path in signed))
            self.assertIn(Path("signtool.exe"), result.tools)

    def test_launch_copy_is_exact_and_removes_direct_run_owner(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            args = self._windows_launch_package(Path(temporary_text))
            with package_game._published_launch_copy(args) as deployed:
                self.assertEqual("package", deployed.name)
                self.assertEqual(args.build_root / "package-runs", deployed.parent.parent)
                self.assertEqual("Windows.x64", package_game.verify_unit(deployed)["target"])
                run = deployed.parent
                self.assertTrue(run.is_dir())
            self.assertFalse(run.exists())

    def test_windows_launch_waits_then_removes_run_copy(self) -> None:
        class FinishedProcess:
            pid = 1

            def __init__(self) -> None:
                self.finished = False
                self.waited = False

            def poll(self) -> int | None:
                return 0 if self.finished else None

            def wait(self, timeout: float | None = None) -> int:
                del timeout
                self.waited = True
                self.finished = True
                return 0

        with tempfile.TemporaryDirectory() as temporary_text:
            args = self._windows_launch_package(Path(temporary_text))
            process = FinishedProcess()
            with mock.patch.object(package_game.subprocess, "Popen", return_value=process) as popen, \
                    mock.patch.object(package_game, "_wait_for_launch_start"):
                package_game._launch(args)
            self.assertTrue(process.waited)
            self.assertEqual(0, popen.call_args.kwargs["creationflags"] & package_game.subprocess.CREATE_BREAKAWAY_FROM_JOB)
            self.assertFalse(any((args.build_root / "package-runs").iterdir()))

    def test_windows_launch_spawn_error_removes_run_copy(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            args = self._windows_launch_package(Path(temporary_text))
            with mock.patch.object(package_game.subprocess, "Popen", side_effect=OSError("denied")):
                with self.assertRaisesRegex(PackagingError, "could not be started"):
                    package_game._launch(args)
            self.assertFalse(any((args.build_root / "package-runs").iterdir()))

    def test_web_launch_waits_stops_server_and_removes_run_copy(self) -> None:
        class FakeServer:
            server_port = 43123

            def __init__(self) -> None:
                self.shutdown_called = False
                self.close_called = False

            @staticmethod
            def serve_forever() -> None:
                return None

            def shutdown(self) -> None:
                self.shutdown_called = True

            def server_close(self) -> None:
                self.close_called = True

        class FakeThread:
            def __init__(self) -> None:
                self.started = False
                self.joined = False

            def start(self) -> None:
                self.started = True

            def join(self, timeout: float | None = None) -> None:
                del timeout
                self.joined = True

        class FinishedProcess:
            pid = 1

            def __init__(self) -> None:
                self.finished = False

            def poll(self) -> int | None:
                return 0 if self.finished else None

            def wait(self, timeout: float | None = None) -> int:
                del timeout
                self.finished = True
                return 0

        with tempfile.TemporaryDirectory() as temporary_text:
            args = self._web_launch_package(Path(temporary_text))
            server = FakeServer()
            thread = FakeThread()
            browser = FinishedProcess()
            with mock.patch.object(package_game.http.server, "ThreadingHTTPServer", return_value=server), \
                    mock.patch.object(package_game.threading, "Thread", return_value=thread), \
                    mock.patch.object(package_game, "_chrome", return_value=Path("browser.exe")), \
                    mock.patch.object(package_game.subprocess, "Popen", return_value=browser) as popen, \
                    mock.patch.object(package_game, "_wait_for_launch_start"):
                package_game._launch(args)
            command = popen.call_args.args[0]
            profile_argument = next(str(value) for value in command if str(value).startswith("--user-data-dir="))
            self.assertNotIn("\\package\\browser-profile", profile_argument)
            self.assertTrue(thread.started and thread.joined)
            self.assertTrue(server.shutdown_called and server.close_called)
            self.assertFalse(any((args.build_root / "package-runs").iterdir()))

    def test_web_browser_spawn_error_stops_server_and_removes_run_copy(self) -> None:
        server = mock.Mock(server_port=43123)
        thread = mock.Mock()
        with tempfile.TemporaryDirectory() as temporary_text:
            args = self._web_launch_package(Path(temporary_text))
            with mock.patch.object(package_game.http.server, "ThreadingHTTPServer", return_value=server), \
                    mock.patch.object(package_game.threading, "Thread", return_value=thread), \
                    mock.patch.object(package_game, "_chrome", return_value=Path("browser.exe")), \
                    mock.patch.object(package_game.subprocess, "Popen", side_effect=OSError("denied")):
                with self.assertRaisesRegex(PackagingError, "browser could not be started"):
                    package_game._launch(args)
            server.shutdown.assert_called_once_with()
            server.server_close.assert_called_once_with()
            thread.join.assert_called_once_with(timeout=5)
            self.assertFalse(any((args.build_root / "package-runs").iterdir()))

    def test_third_party_inventory_is_complete_and_documented(self) -> None:
        engine_root = SCRIPTS.parent
        components = third_party_notices.load_components(engine_root)
        documented = (engine_root / "third_party/THIRD_PARTY_LICENSES.md").read_text(encoding="utf-8")
        for component in components:
            self.assertIn(component.name, documented, component.id)
        windows = {component.id for component in third_party_notices.select_components(components, "game", "windows")}
        # meshoptimizer is compiled into the renderer, so every player ships its notice.
        self.assertTrue({"bgfx", "bx", "bimg", "meshoptimizer", "lua", "jolt", "miniaudio", "directx-headers", "zstd"} <= windows)
        self.assertFalse({"glslang", "heroicons", "nvtt", "dawn", "androidx"} & windows)
        self.assertIn("androidx", {c.id for c in third_party_notices.select_components(components, "game", "android")})
        self.assertIn("dawn", {c.id for c in third_party_notices.select_components(components, "game", "webgpu")})
        self.assertNotIn("directx-headers", {c.id for c in third_party_notices.select_components(components, "game", "linux")})
        self.assertEqual(len(third_party_notices.select_components(components, "engine", None)), len(components))

    def test_staged_licenses_carry_notices_texts_and_sbom(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            stage = Path(temporary_text)
            args = argparse.Namespace(
                engine_root=SCRIPTS.parent, target="Windows.x64", product_name="Sample Game", version="1.2.3",
            )
            package_game._stage_licenses(args, stage)
            notices = (stage / "THIRD_PARTY_NOTICES.txt").read_text(encoding="utf-8")
            self.assertIn("meshoptimizer", notices)
            self.assertIn("Arseny Kapoulkine", notices)
            self.assertTrue((stage / "Licenses/meshoptimizer.txt").is_file())
            # The Apache License text is shared by several components but staged once.
            self.assertEqual(sum(1 for _ in (stage / "Licenses").glob("*.txt")),
                             len({f for c in third_party_notices.select_components(
                                 third_party_notices.load_components(SCRIPTS.parent), "game", "windows")
                                  for f in c.license_files}))
            sbom = json.loads((stage / "sbom.cdx.json").read_text(encoding="utf-8"))
            self.assertEqual((sbom["bomFormat"], sbom["specVersion"]), ("CycloneDX", "1.5"))
            self.assertEqual(sbom["metadata"]["component"], {
                "type": "application", "bom-ref": "product", "name": "Sample Game", "version": "1.2.3",
            })
            refs = {component["bom-ref"] for component in sbom["components"]}
            self.assertIn("meshoptimizer", refs)
            patched = {c["bom-ref"] for c in sbom["components"] if "pedigree" in c}
            self.assertTrue({"cgltf", "bimg", "stb"} <= patched)
            self.assertIn("Modified by 21kb", notices)
            self.assertEqual(set(sbom["dependencies"][0]["dependsOn"]), refs)
            again = third_party_notices.build_sbom("Sample Game", "1.2.3", [])
            self.assertEqual(again["serialNumber"], sbom["serialNumber"])

    def test_missing_third_party_license_text_fails_packaging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            (root / "third_party").mkdir()
            (root / "third_party/third_party_manifest.json").write_text(json.dumps({
                "schema": "21kb.third-party/v1",
                "components": [{
                    "id": "gone", "name": "Gone", "version": "1", "license": "MIT", "copyright": "c",
                    "url": "https://example.invalid", "path": "third_party/gone",
                    "licenseFiles": ["third_party/gone/LICENSE"], "scope": "game",
                }],
            }), encoding="utf-8")
            args = argparse.Namespace(engine_root=root, target="Windows.x64", product_name="G", version="1")
            with self.assertRaisesRegex(PackagingError, "license file is missing"):
                package_game._stage_licenses(args, root / "stage")

if __name__ == "__main__":
    unittest.main()

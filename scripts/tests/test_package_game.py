from __future__ import annotations

import argparse
import io
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
from package_contract import PackagingError, seal_unit  # noqa: E402
from windows_pe_resources import _version_resource, _version_tuple  # noqa: E402


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
            self.assertEqual(job / "cook" / "Game.kbpack", pack)

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
            with mock.patch.object(Path, "home", return_value=root / "home"), \
                    mock.patch.object(package_linux_guest, "_extract_input"), \
                    mock.patch.object(package_linux_guest, "_verify_package_unit", return_value=receipt), \
                    mock.patch.object(package_linux_guest, "_verify_linux_runtime", return_value=Path("Game")), \
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

            def start(*_args: object, **_kwargs: object) -> RunningProcess:
                self.assertTrue(lock.active)
                events.append("start")
                return RunningProcess()

            with mock.patch.object(Path, "home", return_value=root / "home"), \
                    mock.patch.object(package_linux_guest, "_extract_input"), \
                    mock.patch.object(package_linux_guest, "_verify_package_unit", return_value=receipt), \
                    mock.patch.object(package_linux_guest, "_verify_linux_runtime", return_value=Path("Game")), \
                    mock.patch.object(package_linux_guest, "_GuestFileLock", return_value=lock), \
                    mock.patch.object(package_linux_guest, "_recorded_launch_is_alive", side_effect=probe), \
                    mock.patch.object(package_linux_guest.shutil, "copytree", side_effect=copytree), \
                    mock.patch.object(package_linux_guest.subprocess, "run", return_value=completed), \
                    mock.patch.object(package_linux_guest.subprocess, "Popen", side_effect=start), \
                    mock.patch.object(package_linux_guest.time, "sleep"), \
                    mock.patch.object(package_linux_guest, "_write_pid_atomically", side_effect=publish_pid):
                package_linux_guest._launch_package(args)

            self.assertEqual(["probe", "deploy", "start", "pid"], events)

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


if __name__ == "__main__":
    unittest.main()

from __future__ import annotations

import sys
import tempfile
import unittest
import warnings
import zipfile
import json
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import android_bundle
import package_game
from package_contract import PackagingError


class AndroidBundleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bundle = self.root / "sample.aab"
        self.pack = self.root / "game.kbpack"
        self.pack.write_bytes(b"cooked scene payload")
        self.anchor = self.root / "anchor.bin"
        self.anchor.write_bytes(b"release trust anchor")
        self.legal = self.root / "legal"
        self.legal.mkdir()
        (self.legal / "THIRD_PARTY_NOTICES.txt").write_text("Component licenses", encoding="utf-8")
        native = bytearray(20)
        native[:6] = b"\x7fELF\x02\x01"
        native[18:20] = b"\xb7\x00"
        self.entries = {
            "BundleConfig.pb": b"config",
            "base/manifest/AndroidManifest.xml": b"manifest",
            "base/lib/arm64-v8a/libkb_game_android.so": bytes(native) + b"Android.ASTC.arm64",
            "base/assets/game.kbpack": self.pack.read_bytes(),
            "base/assets/kb_trust_anchor.bin": self.anchor.read_bytes(),
            "base/assets/THIRD_PARTY_NOTICES.txt": (self.legal / "THIRD_PARTY_NOTICES.txt").read_bytes(),
        }
        self.base_xml = ('<manifest xmlns:android="http://schemas.android.com/apk/res/android" '
                         'package="com.example.sample" android:versionCode="2" android:versionName="1.2">'
                         '<uses-sdk android:minSdkVersion="23" android:targetSdkVersion="36"/>'
                         '<application/></manifest>')
        self.asset_xml = ('<manifest xmlns:dist="http://schemas.android.com/apk/distribution" split="runtimeassets">'
                          '<dist:module dist:type="asset-pack"><dist:delivery>'
                          '<dist:install-time/></dist:delivery></dist:module></manifest>')
        self.tool = mock.patch.object(android_bundle, "run_checked", side_effect=self.run_tool).start()
        self.addCleanup(mock.patch.stopall)

    def run_tool(self, command, **kwargs):
        if "dump" in command:
            return SimpleNamespace(output=self.asset_xml if "--module=runtimeassets" in command else self.base_xml)
        return SimpleNamespace(output="Bundle valid")

    def write(self):
        with zipfile.ZipFile(self.bundle, "w") as archive:
            for name, value in self.entries.items():
                archive.writestr(name, value)

    def validate(self, delivery=False):
        android_bundle.validate_bundle(
            self.bundle, java=self.root / "java", bundletool=self.root / "bundletool.jar",
            profile="Android.ASTC.arm64", pack=self.pack, trust_anchor=self.anchor,
            legal_assets=self.legal, application_id="com.example.sample", version_code=2,
            version_name="1.2", min_sdk=23, target_sdk=36, debuggable=False, asset_delivery=delivery,
        )

    def delivery(self):
        self.entries["runtimeassets/assets/game.kbpack"] = self.entries.pop("base/assets/game.kbpack")
        self.entries["runtimeassets/manifest/AndroidManifest.xml"] = b"asset manifest"

    def test_base_payload_and_manifest(self):
        self.write()
        self.validate()
        self.assertEqual(self.tool.call_count, 2)
        self.assertIn("validate", self.tool.call_args_list[0].args[0])

    def test_install_time_payload(self):
        self.delivery()
        self.write()
        self.validate(True)
        self.assertEqual(self.tool.call_count, 3)

    def test_modified_payload_and_anchor_are_rejected(self):
        for name in ("base/assets/game.kbpack", "base/assets/kb_trust_anchor.bin",
                     "base/assets/THIRD_PARTY_NOTICES.txt"):
            with self.subTest(name=name):
                original = self.entries[name]
                self.entries[name] = b"X" + original[1:]
                self.write()
                with self.assertRaisesRegex(PackagingError, "payload differs"):
                    self.validate()
                self.entries[name] = original
        self.tool.assert_not_called()

    def test_missing_pack_and_native_abi_are_rejected(self):
        pack = self.entries.pop("base/assets/game.kbpack")
        self.write()
        with self.assertRaisesRegex(PackagingError, "missing runtime"):
            self.validate()
        self.entries["base/assets/game.kbpack"] = pack
        self.entries["base/lib/x86/libother.so"] = b"library"
        self.write()
        with self.assertRaisesRegex(PackagingError, "ABI"):
            self.validate()

    def test_wrong_profile_is_rejected(self):
        name = "base/lib/arm64-v8a/libkb_game_android.so"
        self.entries[name] = self.entries[name].replace(b"ASTC", b"ETC2")
        self.write()
        with self.assertRaisesRegex(PackagingError, "texture profile"):
            self.validate()

    def test_manifest_mismatch_and_tool_failure_are_rejected(self):
        self.write()
        self.base_xml = self.base_xml.replace('versionCode="2"', 'versionCode="3"')
        with self.assertRaisesRegex(PackagingError, "manifest differs"):
            self.validate()
        self.tool.side_effect = PackagingError("bundletool validation failed")
        with self.assertRaisesRegex(PackagingError, "bundletool validation failed"):
            self.validate()

    def test_conditional_or_delayed_assets_are_rejected(self):
        self.delivery()
        self.write()
        original = self.asset_xml
        for delivery in ('<dist:on-demand/>', '<dist:install-time><dist:conditions/></dist:install-time>'):
            with self.subTest(delivery=delivery):
                self.asset_xml = original.replace('<dist:install-time/>', delivery)
                with self.assertRaisesRegex(PackagingError, "unconditional install-time"):
                    self.validate(True)

    def test_duplicates_and_unsafe_paths_are_rejected(self):
        self.write()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(self.bundle, "a") as archive:
                archive.writestr("BundleConfig.pb", b"duplicate")
        with self.assertRaisesRegex(PackagingError, "duplicate"):
            self.validate()
        self.entries["../file"] = b"unsafe"
        self.write()
        with self.assertRaisesRegex(PackagingError, "unsafe"):
            self.validate()


class AndroidSigningRequestTests(unittest.TestCase):
    def test_missing_installed_assets_fail_without_waiting_for_timeout(self):
        args = SimpleNamespace(target="Android.ASTC.arm64", android_application_id="com.example.sample")
        def execute(command, **kwargs):
            output = "123" if "pidof" in command else (
                "E/21kb: packaged asset pack was not found" if "logcat" in command else "")
            return SimpleNamespace(output=output)
        with mock.patch.object(package_game, "run_checked", side_effect=execute) as checked, \
                mock.patch.object(package_game.time, "sleep") as sleep:
            with self.assertRaisesRegex(PackagingError, "packaged asset pack was not found"):
                package_game._verify_android_first_frame(args, Path("stage/Sample.aab"), Path("adb"), install=False)
        sleep.assert_not_called()
        commands = [call.args[0] for call in checked.call_args_list]
        self.assertEqual(sum("force-stop" in command for command in commands), 2)
        self.assertFalse(any("install" in command for command in commands))

    def test_bundle_launch_installs_split_set_and_probes_without_apk_install(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bundle = root / "platform/android/app/build/outputs/bundle/astcDebug/sample.aab"
            bundle.parent.mkdir(parents=True)
            bundle.write_bytes(b"bundle fixture")
            wrapper = root / "gradle-wrapper.jar"
            wrapper.touch()
            stage, job = root / "stage", root / "job"
            stage.mkdir()
            job.mkdir()
            args = SimpleNamespace(engine_root=root, configuration="Development", executable_name="Sample",
                android_bundletool=root / "bundletool.jar", target="Android.ASTC.arm64", trust_anchor=root / "anchor",
                android_application_id="com.example.sample", android_version_code=2, version="1.2",
                android_min_sdk=23, android_target_sdk=36, launch=True)
            with mock.patch.object(package_game, "_java", return_value=root / "java.exe"), \
                    mock.patch.object(package_game, "validate_bundle") as validate, \
                    mock.patch.object(package_game, "_android_adb", return_value=root / "adb.exe"), \
                    mock.patch.object(package_game, "run_checked") as execute, \
                    mock.patch.object(package_game, "_verify_android_first_frame") as probe, \
                    mock.patch.dict("os.environ", {"ANDROID_SERIAL": "test-device"}):
                result = package_game._finish_android_bundle(args, root / "pack", stage, job, "astc", "Debug",
                    root / "legal", root / "build-tools", wrapper)
            self.assertEqual((stage / "Sample-astc.aab").read_bytes(), b"bundle fixture")
            self.assertTrue(validate.call_args.kwargs["asset_delivery"])
            self.assertIn("build-apks", execute.call_args_list[0].args[0])
            self.assertIn("install-apks", execute.call_args_list[1].args[0])
            for call in execute.call_args_list:
                self.assertIn("--device-id=test-device", call.args[0])
            probe.assert_called_once_with(args, stage / "Sample-astc.aab", root / "adb.exe", install=False)
            self.assertIsNotNone(result.first_frame)

            # An installer failure must never produce first-frame evidence.
            # Inject the device's storage error at the command boundary without
            # exhausting storage on a shared physical device.
            with mock.patch.object(package_game, "_java", return_value=root / "java.exe"), \
                    mock.patch.object(package_game, "validate_bundle"), \
                    mock.patch.object(package_game, "_android_adb", return_value=root / "adb.exe"), \
                    mock.patch.object(package_game, "run_checked", side_effect=[
                        None, PackagingError("INSTALL_FAILED_INSUFFICIENT_STORAGE")]), \
                    mock.patch.object(package_game, "_verify_android_first_frame") as probe, \
                    mock.patch.object(package_game, "_try_android_force_stop") as cleanup, \
                    mock.patch.dict("os.environ", {"ANDROID_SERIAL": "test-device"}):
                with self.assertRaisesRegex(PackagingError, "INSUFFICIENT_STORAGE"):
                    package_game._finish_android_bundle(args, root / "pack", stage, job, "astc", "Debug",
                        root / "legal", root / "build-tools", wrapper)
            probe.assert_not_called()
            cleanup.assert_called_once_with(root / "adb.exe", "com.example.sample", stage)

    def test_apk_and_bundle_protocols_preserve_session_and_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for kind in ("apk", "aab"):
                with self.subTest(kind=kind):
                    job = root / kind
                    job.mkdir()
                    signed = job / f"signed.{kind}"
                    def broker(command, **kwargs):
                        request = json.loads((job / "android-signing-request.json").read_text())
                        self.assertEqual(request["schema"], 2 if kind == "aab" else 1)
                        self.assertEqual(len(request), 8)
                        self.assertEqual(request["keystore"], str(root / "key.p12"))
                        self.assertEqual(request["keyAlias"], "upload")
                        self.assertEqual(request["inputPackage" if kind == "aab" else "inputApk"],
                                         str(job / f"unsigned.{kind}"))
                        if kind == "aab":
                            self.assertEqual(request["format"], "aab")
                            self.assertNotIn("apksignerJar", request)
                        else:
                            self.assertEqual(request["apksignerJar"], str(root / "apksigner.jar"))
                        signed.write_bytes(b"signed fixture")
                        (job / "android-signing-response.json").write_text(json.dumps({
                            "schema": 1, "session": request["session"], "succeeded": True,
                        }))
                    with mock.patch.object(package_game, "_java", return_value=root / "java.exe"), \
                            mock.patch.object(package_game, "run_checked", side_effect=broker):
                        package_game._request_android_signature(
                            root / "broker.exe", root / "key.p12", "upload",
                            job / f"unsigned.{kind}", signed, root / "apksigner.jar", job,
                            package_format=kind)

    def test_bundle_refuses_mismatched_response_session(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            signed = root / "signed.aab"
            def broker(command, **kwargs):
                signed.write_bytes(b"unrelated artifact")
                (root / "android-signing-response.json").write_text(json.dumps({
                    "schema": 1, "session": "wrong-session", "succeeded": True,
                }))
            with mock.patch.object(package_game, "_java", return_value=root / "java.exe"), \
                    mock.patch.object(package_game, "run_checked", side_effect=broker):
                with self.assertRaisesRegex(PackagingError, "refused"):
                    package_game._request_android_signature(root / "broker.exe", root / "key.p12", "upload",
                        root / "unsigned.aab", signed, root / "apksigner.jar", root, package_format="aab")


if __name__ == "__main__":
    unittest.main()

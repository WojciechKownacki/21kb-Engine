"""Check packaging against the shipped browser shell, without compiling a runtime."""
from pathlib import Path
import argparse
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_game


class WebPackageShellTests(unittest.TestCase):
    def test_fallback_configuration_survives_minified_shell(self):
        shell = '<!doctypehtml><html><head><title>Game</title></head><body><script async src=Game.js></script></body></html>'
        result = package_game._configure_web_fallback(shell, 'webgl/Game.html?value="&')
        self.assertIn('content="webgl/Game.html?value=&quot;&amp;"', result)
        self.assertLess(result.index('kb-webgl-fallback'), result.index('</head>'))
        self.assertIn('<script async src=Game.js></script>', result)

    def test_fallback_copies_verified_matching_content_and_rejects_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, stage = root / "fallback", root / "stage"
            source.mkdir()
            stage.mkdir()
            entry = source / "Sample Game.html"
            entry.write_text("fixture", encoding="utf-8")
            receipt = {"target": "WebGL.wasm32", "inputs": {"projectSha256": "project"},
                "product": {"executableName": "Sample Game"}}
            evidence = package_game._first_frame_result("WebGL.wasm32", source).receipt_fields()
            package_game.seal_unit(source, receipt, runtime_first_frame=evidence)
            args = argparse.Namespace(target="WebGPU.wasm32", project=root / "Sample.kbproject")
            with mock.patch.object(package_game, "_project_source_fingerprint", return_value="project"):
                url = package_game._stage_web_fallback(args, stage, source)
            self.assertEqual("webgl/Sample%20Game.html", url)
            self.assertEqual(package_game.verify_unit(source), package_game.verify_unit(stage / "webgl"))
            with mock.patch.object(package_game, "_project_source_fingerprint", return_value="changed"):
                with self.assertRaisesRegex(package_game.PackagingError, "different project"):
                    package_game._stage_web_fallback(args, stage, source)
            entry.write_text("tampered", encoding="utf-8")
            with self.assertRaises(package_game.PackagingError):
                package_game._stage_web_fallback(args, stage, source)

    def test_current_shell_accepts_metadata_and_renamed_runtime(self):
        engine = Path(__file__).resolve().parents[2]
        shell = (engine / "sources/game/src/web/WebGameShell.html").read_text(encoding="utf-8")
        for target in ("WebGL.wasm32", "WebGPU.wasm32"):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                stage = root / "stage"
                stage.mkdir()
                output = root / "packages" / target / "Release" / "bin"
                output.mkdir(parents=True)
                (output / "Game.html").write_text(shell.replace("{{{ SCRIPT }}}",
                    '<script async src="Game.js"></script>'), encoding="utf-8")
                (output / "Game.js").write_text('WebAssembly; const file="Game.wasm";', encoding="utf-8")
                (output / "Game.wasm").write_bytes(b"\0asm")
                pack = root / "Game.kbpack"
                pack.write_bytes(b"fixture")
                args = argparse.Namespace(target=target, configuration="Release", build_root=root,
                    engine_root=engine, executable_name="Sample", product_name="Sample & Test",
                    publisher="Sample Publisher", version="1.2.3", application_icon=None,
                    webgl_fallback=root / "fallback" if target == "WebGPU.wasm32" else None)
                with mock.patch.object(package_game, "_emsdk_root", return_value=root), \
                        mock.patch.object(package_game, "_ninja_for_cmake", return_value=root / "ninja"), \
                        mock.patch.object(package_game, "run_checked"), \
                        mock.patch.object(package_game, "_build_targets"), \
                        mock.patch.object(package_game, "_stage_licenses"), \
                        mock.patch.object(package_game, "_stage_web_fallback", return_value="webgl/Sample%20Game.html") as fallback, \
                        mock.patch.object(package_game, "_verify_web_first_frame", return_value=root / "browser") as verify, \
                        mock.patch.object(package_game, "_first_frame_result"):
                    package_game._stage_web(args, root / "cmake", pack, stage, root)
                html = (stage / "Sample.html").read_text(encoding="utf-8")
                javascript = (stage / "Sample.js").read_text(encoding="utf-8")
                self.assertIn('<title>Sample &amp; Test</title>', html)
                self.assertIn('name="author" content="Sample Publisher"', html)
                self.assertIn('name="application-version" content="1.2.3"', html)
                self.assertIn('src="Sample.js"', html)
                self.assertIn('"Sample.wasm"', javascript)
                self.assertIn("globalThis.kbReportRuntimeError?.('abort', reason)", html)
                self.assertIn("function kbReportRuntimeError", html)
                if target == "WebGPU.wasm32":
                    fallback.assert_called_once_with(args, stage, args.webgl_fallback)
                    self.assertLess(html.index('name="kb-webgl-fallback"'), html.index('src="Sample.js"'))
                    self.assertIn('"webgl/Sample%20Game.html"', html)
                else:
                    fallback.assert_not_called()
                verify.assert_called_once()


if __name__ == "__main__":
    unittest.main()

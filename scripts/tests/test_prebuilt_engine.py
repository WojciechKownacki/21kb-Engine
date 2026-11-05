import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import prebuilt_engine
from package_contract import PackagingError, sha256_file


class PrebuiltEngineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.files = {}
        for relative in prebuilt_engine.REQUIRED_FILES:
            file = self.root / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(b"integrity fixture")
            self.files[relative] = sha256_file(file)
        self.write()

    def write(self):
        (self.root / prebuilt_engine.MANIFEST).write_text(json.dumps(dict(
            schema=prebuilt_engine.SCHEMA, target="Windows.x64", engineConfiguration="Release", files=self.files)))

    def test_mutated_and_missing_payloads_are_rejected(self):
        original = prebuilt_engine.fingerprint(self.root)
        prebuilt_engine.verify_targets(self.root, self.root, ["kb_game"])
        game = self.root / "bin/kb_game.exe"
        game.write_bytes(b"modified tool")
        with self.assertRaisesRegex(PackagingError, "SHA-256 mismatch"):
            prebuilt_engine.verify_targets(self.root, self.root, ["kb_game"])
        with self.assertRaises(PackagingError):
            prebuilt_engine.fingerprint(self.root)
        game.unlink()
        with self.assertRaises(PackagingError):
            prebuilt_engine.fingerprint(self.root)
        self.assertEqual(len(original), 64)

    def test_manifest_cannot_escape_installation(self):
        self.files["../outside.exe"] = "0" * 64
        self.write()
        with self.assertRaises(PackagingError):
            prebuilt_engine.load(self.root)

    def test_wrong_build_root_and_unknown_target_are_rejected(self):
        with self.assertRaises(PackagingError):
            prebuilt_engine.verify_targets(self.root, self.root / "other", ["kb_game"])
        with self.assertRaises(PackagingError):
            prebuilt_engine.verify_targets(self.root, self.root, ["unknown"])

    def test_required_tool_cannot_be_omitted(self):
        del self.files["bin/kb_cooker.exe"]
        self.write()
        with self.assertRaises(PackagingError):
            prebuilt_engine.load(self.root)


if __name__ == "__main__":
    unittest.main()

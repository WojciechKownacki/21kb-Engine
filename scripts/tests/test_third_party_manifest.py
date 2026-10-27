from __future__ import annotations

import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / "scripts"
sys.path.insert(0, str(SCRIPTS))

import third_party_notices  # noqa: E402

MANIFEST = ROOT / "third_party" / "third_party_manifest.json"
LICENSES_DOC = ROOT / "third_party" / "THIRD_PARTY_LICENSES.md"

# Folders of third_party/ that hold a component's license inventory entry only by another path, or hold no
# component at all. Each one must be named in THIRD_PARTY_LICENSES.md or be build infrastructure.
NOT_IN_MANIFEST_BY_PATH = {
    # A standalone copy of bgfx; the build uses the one inside third_party/bgfx.cmake.
    "bgfx": "bgfx",
    # Our patches to third_party/bgfx.cmake and the license texts of fetched components.
    "bgfx-patches": None,
    "licenses": None,
}

# Tracked fonts outside third_party/ that no product ships (fuzzer inputs), by folder.
UNSHIPPED_FONT_FOLDERS = ("fuzz/corpus/",)


def _components() -> list[third_party_notices.Component]:
    return third_party_notices.load_components(ROOT)


def _tracked_files() -> list[str]:
    try:
        output = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return []
    return output.splitlines()


def _cmake_files() -> list[Path]:
    """The build scripts of the repository: tracked files when git is there, else a walk that skips build output."""
    tracked = [file for file in _tracked_files() if not file.startswith("third_party/")]
    candidates = tracked or [path.relative_to(ROOT).as_posix() for path in ROOT.rglob("*")
                             if path.is_file() and not {"build", ".cxx", "third_party", "Saved"} & set(path.relative_to(ROOT).parts)]
    return [ROOT / file for file in candidates if file.endswith(".cmake") or file.endswith("CMakeLists.txt")]


def _fetches() -> list[tuple[Path, str, str]]:
    """Every FetchContent_Declare of the build: file, URL and URL_HASH or GIT_TAG ('' when unpinned)."""
    found = []
    for path in _cmake_files():
        text = path.read_text(encoding="utf-8", errors="replace")
        for block in re.finditer(r"FetchContent_Declare\((.*?)\n\)", text, re.DOTALL):
            body = block.group(1)
            url = re.search(r"\bURL\s+(\S+)", body) or re.search(r"\bGIT_REPOSITORY\s+(\S+)", body)
            pin = re.search(r"\bURL_HASH\s+(\S+)", body) or re.search(r"\bGIT_TAG\s+(\S+)", body)
            found.append((path, url.group(1) if url else "", pin.group(1) if pin else ""))
    return found


class ThirdPartyManifestTests(unittest.TestCase):
    def test_every_component_is_complete(self) -> None:
        # load_components rejects a missing license file, an unknown scope or platform and a duplicate id.
        components = _components()
        self.assertGreater(len(components), 40)
        document = LICENSES_DOC.read_text(encoding="utf-8").lower()
        for component in components:
            self.assertTrue(component.copyright.strip(), f"{component.id}: no copyright line")
            self.assertTrue(component.license.strip(), f"{component.id}: no license")
            self.assertTrue(component.version.strip(), f"{component.id}: no version")
            self.assertTrue(component.url.startswith("https://"), f"{component.id}: no https url")
            self.assertTrue(
                component.name.lower() in document or component.id.lower() in document,
                f"{component.id}: not described in third_party/THIRD_PARTY_LICENSES.md")

    def test_every_component_is_in_the_repository_or_fetched(self) -> None:
        fetches = [(url, pin) for _, url, pin in _fetches()]
        for component in _components():
            if (ROOT / component.path).exists():
                continue
            if component.path.startswith("fetched at configure time"):
                self.assertTrue(
                    any(component.version in url and component.id.split("-")[0] in url.lower() for url, _ in fetches),
                    f"{component.id}: the manifest says it is fetched, but no FetchContent URL names {component.version}")
            elif component.platforms == ("android",):
                gradle = (ROOT / "platform" / "android" / "app" / "build.gradle").read_text(encoding="utf-8")
                self.assertIn("androidx" if component.id == "androidx" else "games-controller", gradle,
                              f"{component.id}: not a dependency of the Android player")
            else:
                self.fail(f"{component.id}: path {component.path!r} does not exist and is not a fetched or Gradle dependency")

    def test_every_fetched_dependency_is_pinned_and_in_the_manifest(self) -> None:
        components = _components()
        fetches = _fetches()
        self.assertGreaterEqual(len(fetches), 4, "the build no longer fetches what this test knows about")
        for path, url, pin in fetches:
            where = f"{path.relative_to(ROOT).as_posix()}: {url}"
            self.assertTrue(url, f"{where}: a FetchContent_Declare without URL or GIT_REPOSITORY")
            self.assertTrue(pin, f"{where}: not pinned (URL_HASH or GIT_TAG)")
            self.assertTrue(
                any(component.version in url and component.id.split("-")[0] in url.lower() for component in components),
                f"{where}: not in third_party/third_party_manifest.json with this version; add the component, its license "
                "text and a section of third_party/THIRD_PARTY_LICENSES.md before building it into a product")

    def test_every_vendored_folder_is_in_the_manifest_or_explained(self) -> None:
        components = _components()
        paths = [component.path for component in components]
        document = LICENSES_DOC.read_text(encoding="utf-8")
        for folder in sorted(path.name for path in (ROOT / "third_party").iterdir() if path.is_dir()):
            relative = f"third_party/{folder}"
            if any(path == relative or path.startswith(relative + "/") for path in paths):
                continue
            self.assertIn(folder, NOT_IN_MANIFEST_BY_PATH,
                          f"{relative} is vendored but no manifest component has a path in it")
            mention = NOT_IN_MANIFEST_BY_PATH[folder]
            if mention is not None:
                self.assertIn(f"`{relative}`", document, f"{relative} is not described in THIRD_PARTY_LICENSES.md")

    def test_fonts_outside_third_party_have_a_manifest_license(self) -> None:
        tracked = _tracked_files()
        if not tracked:
            self.skipTest("git is not available")
        paths = [component.path for component in _components()]
        for file in tracked:
            if file.startswith("third_party/") or not file.lower().endswith((".ttf", ".otf", ".ttc", ".woff", ".woff2")):
                continue
            if file.startswith(UNSHIPPED_FONT_FOLDERS):
                continue
            self.assertTrue(any(file.startswith(path + "/") for path in paths),
                            f"{file}: a font that no manifest component covers")

    def test_notices_build_for_the_engine_and_every_game_platform(self) -> None:
        components = _components()
        for product, platform in (("engine", None), *[("game", name) for name in third_party_notices.PLATFORMS]):
            with self.subTest(product=product, platform=platform):
                selected = third_party_notices.select_components(components, product, platform)
                self.assertTrue(selected)
                destination = Path(tempfile.mkdtemp())
                try:
                    third_party_notices.stage_notices(ROOT, destination, selected, "Test product")
                    notices = (destination / "THIRD_PARTY_NOTICES.txt").read_text(encoding="utf-8")
                    for component in selected:
                        self.assertIn(component.name, notices)
                    self.assertTrue(any((destination / "Licenses").iterdir()))
                finally:
                    shutil.rmtree(destination, ignore_errors=True)

    def test_the_sbom_lists_every_selected_component(self) -> None:
        components = _components()
        sbom = third_party_notices.build_sbom("Test product", "1.0", third_party_notices.select_components(components, "engine", None))
        self.assertEqual(len(sbom["components"]), len(components))
        json.dumps(sbom)


if __name__ == "__main__":
    unittest.main()

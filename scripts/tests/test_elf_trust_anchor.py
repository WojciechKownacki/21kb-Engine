from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import elf_trust_anchor  # noqa: E402
import package_game  # noqa: E402
import package_linux_guest  # noqa: E402
from elf_trust_anchor import (  # noqa: E402
    SECTION_NAME,
    SLOT_BYTES,
    SLOT_MAGIC,
    ElfTrustAnchorError,
    embed_trust_anchor,
    find_trust_anchor_slot,
    read_trust_anchor,
)
from package_contract import PackagingError  # noqa: E402

ENGINE = Path(__file__).resolve().parents[2]


def make_slot(anchor: bytes = b"") -> bytes:
    slot = SLOT_MAGIC + struct.pack("<II", len(anchor), 0) + anchor
    return slot + bytes(SLOT_BYTES - len(slot))


def make_elf(sections: list[tuple[bytes, int, bytes]]) -> bytes:
    """A minimal 64-bit little-endian x86-64 ELF executable laid out as a linker writes one:
    header, section contents, the section name table, then the section header table."""
    image = bytearray(64)
    image[0:8] = b"\x7fELF\x02\x01\x01\x00"
    struct.pack_into("<HHI", image, 0x10, 2, 62, 1)
    names = bytearray(b"\0")
    placed: list[tuple[int, int]] = []
    for name, _kind, contents in sections:
        while len(image) % 16:
            image.append(0)
        placed.append((len(image), len(names)))
        names += name + b"\0"
        image += contents
    names_name = len(names)
    names += b".shstrtab\0"
    names_offset = len(image)
    image += names
    while len(image) % 8:
        image.append(0)
    table = len(image)
    count = len(sections) + 2
    image += bytes(count * 64)
    for index, (_name, kind, contents) in enumerate(sections):
        header = table + (index + 1) * 64
        struct.pack_into("<IIQ", image, header, placed[index][1], kind, 2)
        struct.pack_into("<QQ", image, header + 0x18, placed[index][0], len(contents))
    names_header = table + (count - 1) * 64
    struct.pack_into("<II", image, names_header, names_name, 3)
    struct.pack_into("<QQ", image, names_header + 0x18, names_offset, len(names))
    struct.pack_into("<Q", image, 0x28, table)
    struct.pack_into("<HHHHHH", image, 0x34, 64, 56, 0, 64, count, count - 1)
    return bytes(image)


def make_player(slot: bytes | None = None, kind: int = 1) -> bytes:
    sections = [(b".text", 1, b"\xc3" * 48)]
    if slot is not None:
        sections.append((SECTION_NAME, kind, slot))
    sections.append((b".data", 1, b"\x11" * 32))
    return make_elf(sections)


ANCHOR = b"21KBTRST" + bytes(range(106)) + b"\x07\x00Pub.Game"


class ElfTrustAnchorTests(unittest.TestCase):
    def test_constants_match_the_engine_reader(self) -> None:
        header = (ENGINE / "sources/engine/include/engine/security/ReleaseKeys.hpp").read_text(encoding="utf-8")
        self.assertEqual(
            SECTION_NAME.decode(),
            re.search(r'kTrustAnchorElfSectionName = "([^"]+)"', header).group(1),  # type: ignore[union-attr]
        )
        self.assertEqual(SLOT_BYTES, int(re.search(r"kTrustAnchorSlotBytes = (\d+)U", header).group(1)))  # type: ignore[union-attr]
        self.assertEqual(SLOT_MAGIC.decode(), re.search(r'kTrustAnchorSlotMagic = "([^"]+)"', header).group(1))  # type: ignore[union-attr]
        source = (ENGINE / "sources/engine/src/security/ReleaseKeys.cpp").read_text(encoding="utf-8")
        self.assertIn(f'section("{SECTION_NAME.decode()}")', source)

    def test_embeds_in_place_and_reads_back(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            player = Path(temporary_text) / "Game"
            original = make_player(make_slot())
            player.write_bytes(original)
            self.assertIsNone(read_trust_anchor(player))
            embed_trust_anchor(player, ANCHOR)
            self.assertEqual(ANCHOR, read_trust_anchor(player))
            filled = player.read_bytes()
            offset, size = find_trust_anchor_slot(filled)  # type: ignore[misc]
            self.assertEqual(len(original), len(filled))
            self.assertEqual(original[:offset], filled[:offset])
            self.assertEqual(original[offset + size:], filled[offset + size:])
            embed_trust_anchor(player, ANCHOR)
            self.assertEqual(filled, player.read_bytes())
            with self.assertRaisesRegex(ElfTrustAnchorError, "different trust anchor"):
                embed_trust_anchor(player, ANCHOR[:-1] + b"X")
            self.assertEqual(filled, player.read_bytes())

    def test_refuses_players_without_a_usable_slot(self) -> None:
        cases = {
            "no slot": make_player(None),
            "nobits slot": make_player(make_slot(), kind=8),
            "short slot": make_player(make_slot()[:-16]),
            "wrong magic": make_player(b"X" + make_slot()[1:]),
            "garbage after anchor": make_player(make_slot(b"abc")[:-1] + b"\x01"),
            "not elf": b"MZ" + bytes(200),
            "32-bit": b"\x7fELF\x01" + make_player(make_slot())[5:],
        }
        with tempfile.TemporaryDirectory() as temporary_text:
            for label, image in cases.items():
                with self.subTest(label):
                    player = Path(temporary_text) / label.replace(" ", "_")
                    player.write_bytes(image)
                    with self.assertRaises(ElfTrustAnchorError):
                        embed_trust_anchor(player, ANCHOR)
                    self.assertEqual(image, player.read_bytes())
            player = Path(temporary_text) / "Game"
            player.write_bytes(make_player(make_slot()))
            with self.assertRaisesRegex(ElfTrustAnchorError, "does not fit"):
                embed_trust_anchor(player, bytes(SLOT_BYTES))
            twice = make_elf([(SECTION_NAME, 1, make_slot()), (SECTION_NAME, 1, make_slot())])
            player.write_bytes(twice)
            with self.assertRaisesRegex(ElfTrustAnchorError, "malformed"):
                embed_trust_anchor(player, ANCHOR)

    def test_guest_fills_the_slot_only_when_the_host_sent_an_anchor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            incoming = root / "incoming"
            incoming.mkdir()
            player = root / "Game"
            player.write_bytes(make_player(make_slot()))
            package_linux_guest._embed_trust_anchor(incoming, player)
            self.assertIsNone(read_trust_anchor(player))
            (incoming / "trust-anchor.bin").write_bytes(ANCHOR)
            package_linux_guest._embed_trust_anchor(incoming, player)
            self.assertEqual(ANCHOR, read_trust_anchor(player))
            player.write_bytes(make_player(None))
            with self.assertRaisesRegex(package_linux_guest.GuestError, "trust anchor"):
                package_linux_guest._embed_trust_anchor(incoming, player)

    def _linux_stage(self, root: Path, player_bytes: bytes) -> argparse.Namespace:
        stage = root / "stage"
        stage.mkdir()
        (stage / "Game").write_bytes(player_bytes)
        (stage / "Game.kbpack").write_bytes(b"pack")
        anchor = root / "trust-anchor.bin"
        anchor.write_bytes(ANCHOR)
        receipt = {
            "schema": 1,
            "configuration": "Release",
            "engineSha256": "0" * 64,
            "executableSha256": hashlib.sha256(player_bytes).hexdigest(),
            "assetPackSha256": hashlib.sha256(b"pack").hexdigest(),
            "firstFrame": True,
        }
        (stage / "linux-build.receipt.json").write_text(json.dumps(receipt), encoding="utf-8")
        return argparse.Namespace(
            executable_name="Game", configuration="Release", engine_fingerprint="0" * 64, trust_anchor=anchor,
        )

    def test_host_refuses_a_linux_player_without_the_release_anchor(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = self._linux_stage(root, make_player(make_slot(ANCHOR)))
            package_game._verify_linux_stage(root / "stage", args)
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = self._linux_stage(root, make_player(make_slot()))
            with self.assertRaisesRegex(PackagingError, "trust anchor"):
                package_game._verify_linux_stage(root / "stage", args)
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            args = self._linux_stage(root, make_player(make_slot(b"another anchor")))
            with self.assertRaisesRegex(PackagingError, "trust anchor"):
                package_game._verify_linux_stage(root / "stage", args)

    def test_remote_linux_build_receives_the_anchor_and_its_helper(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_text:
            root = Path(temporary_text)
            scripts = root / "engine" / "scripts"
            scripts.mkdir(parents=True)
            for name in ("package_linux_guest.py", "package_contract.py", "elf_trust_anchor.py"):
                (scripts / name).write_text(name, encoding="ascii")
            anchor = root / "trust-anchor.bin"
            anchor.write_bytes(ANCHOR)
            job = root / "job"
            job.mkdir()
            pack = root / "Game.kbpack"
            pack.write_bytes(b"pack")
            args = argparse.Namespace(
                engine_root=root / "engine",
                configuration="Release",
                executable_name="Game",
                engine_fingerprint="0" * 64,
                linux_host="builder.example",
                linux_user="builder",
                linux_host_key="ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAITest",
                linux_port=22,
                linux_engine_root="/srv/21kb",
                linux_identity=None,
                trust_anchor=anchor,
            )
            with mock.patch.object(package_game, "_required_executable", side_effect=lambda name: Path(name)), \
                    mock.patch.object(package_game, "run_checked") as run, \
                    mock.patch.object(package_game, "_extract_linux_result"):
                with self.assertRaisesRegex(PackagingError, "ELF player"):
                    package_game._stage_linux_remote(args, pack, root / "stage", job)
            uploads = [call.args[0] for call in run.call_args_list if call.args[0][0] == Path("scp")]
            self.assertIn(scripts / "elf_trust_anchor.py", uploads[0])
            with tarfile.open(job / "linux-input.tar.gz", "r:gz") as archive:
                member = archive.extractfile("trust-anchor.bin")
                self.assertIsNotNone(member)
                self.assertEqual(ANCHOR, member.read())  # type: ignore[union-attr]


if __name__ == "__main__":
    unittest.main()

"""Trust anchor slot of a Linux ELF player.

A Linux player reserves its trust anchor at build time: an allocated, read-only section named
SECTION_NAME of exactly SLOT_BYTES, starting with SLOT_MAGIC and otherwise empty
(kExecutableTrustAnchorSlot in sources/engine/src/security/ReleaseKeys.cpp). Packaging fills
the slot in place, so the player's layout, symbols and code stay exactly as linked:

    magic (16 bytes) | anchor length (u32 little-endian) | 0 (u32) | anchor | zero padding

The constants must match kTrustAnchorElfSectionName, kTrustAnchorSlotBytes and
kTrustAnchorSlotMagic in sources/engine/include/engine/security/ReleaseKeys.hpp.
"""

from __future__ import annotations

import os
import struct
import tempfile
from pathlib import Path

SECTION_NAME = b".kb_trust_anchor"
SLOT_BYTES = 1024
SLOT_MAGIC = b"21KB-ANCHOR-SLOT"
_HEADER_BYTES = 24
_SHT_NOBITS = 8
_SECTION_HEADER_BYTES = 64


class ElfTrustAnchorError(RuntimeError):
    pass


def find_trust_anchor_slot(image: bytes) -> tuple[int, int] | None:
    """File offset and size of the slot section of a 64-bit little-endian ELF image, or None
    when it has none. Raises ElfTrustAnchorError on a malformed image."""
    if len(image) < 64 or image[:4] != b"\x7fELF":
        raise ElfTrustAnchorError("player is not an ELF image")
    if image[4] != 2 or image[5] != 1:
        raise ElfTrustAnchorError("player is not a 64-bit little-endian ELF image")
    (table,) = struct.unpack_from("<Q", image, 0x28)
    entry_size, count, names_index = struct.unpack_from("<HHH", image, 0x3A)
    if table == 0:
        return None
    if entry_size != _SECTION_HEADER_BYTES or table > len(image) or len(image) - table < _SECTION_HEADER_BYTES:
        raise ElfTrustAnchorError("player ELF section table is malformed")

    def header(index: int) -> tuple[int, int, int, int]:
        name, kind = struct.unpack_from("<II", image, table + index * _SECTION_HEADER_BYTES)
        offset, size = struct.unpack_from("<QQ", image, table + index * _SECTION_HEADER_BYTES + 0x18)
        return name, kind, offset, size

    if count == 0:
        count = header(0)[3]
    if names_index == 0xFFFF:
        (names_index,) = struct.unpack_from("<I", image, table + 0x28)
    if count > (len(image) - table) // _SECTION_HEADER_BYTES or names_index >= count:
        raise ElfTrustAnchorError("player ELF section table is malformed")
    _, _, names_offset, names_size = header(names_index)
    if names_offset > len(image) or names_size > len(image) - names_offset:
        raise ElfTrustAnchorError("player ELF section names are malformed")
    names = image[names_offset:names_offset + names_size]
    found: tuple[int, int] | None = None
    for index in range(1, count):
        name_offset, kind, offset, size = header(index)
        if name_offset >= len(names):
            raise ElfTrustAnchorError("player ELF section names are malformed")
        end = names.find(b"\0", name_offset)
        name = names[name_offset:] if end < 0 else names[name_offset:end]
        if name != SECTION_NAME:
            continue
        if found is not None or kind == _SHT_NOBITS or offset > len(image) or size > len(image) - offset:
            raise ElfTrustAnchorError("player trust anchor section is malformed")
        found = (offset, size)
    return found


def _slot(image: bytes) -> tuple[int, bytes]:
    located = find_trust_anchor_slot(image)
    if located is None:
        raise ElfTrustAnchorError(f"player has no {SECTION_NAME.decode()} section; it was not built from this engine")
    offset, size = located
    slot = image[offset:offset + size]
    if size != SLOT_BYTES or slot[:len(SLOT_MAGIC)] != SLOT_MAGIC:
        raise ElfTrustAnchorError("player trust anchor slot is not a 21kb trust anchor slot")
    return offset, slot


def _slot_anchor(slot: bytes) -> bytes | None:
    length, reserved = struct.unpack_from("<II", slot, len(SLOT_MAGIC))
    if reserved != 0 or length > SLOT_BYTES - _HEADER_BYTES or any(slot[_HEADER_BYTES + length:]):
        raise ElfTrustAnchorError("player trust anchor slot is malformed")
    return None if length == 0 else slot[_HEADER_BYTES:_HEADER_BYTES + length]


def read_trust_anchor(path: Path) -> bytes | None:
    """The anchor in the player's slot, or None while the slot is empty."""
    return _slot_anchor(_slot(path.read_bytes())[1])


def embed_trust_anchor(path: Path, anchor: bytes) -> None:
    """Fills the player's empty slot with `anchor`, in place and atomically. Filling it again with
    the same anchor changes nothing; a different anchor is refused."""
    if not anchor or len(anchor) > SLOT_BYTES - _HEADER_BYTES:
        raise ElfTrustAnchorError("trust anchor does not fit the player's slot")
    image = path.read_bytes()
    offset, slot = _slot(image)
    current = _slot_anchor(slot)
    if current is not None:
        if current == anchor:
            return
        raise ElfTrustAnchorError("player already carries a different trust anchor")
    filled = SLOT_MAGIC + struct.pack("<II", len(anchor), 0) + anchor
    filled += bytes(SLOT_BYTES - len(filled))
    updated = image[:offset] + filled + image[offset + SLOT_BYTES:]
    mode = path.stat().st_mode
    descriptor, temporary_text = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    temporary = Path(temporary_text)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(updated)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise
    if read_trust_anchor(path) != anchor:
        raise ElfTrustAnchorError("player trust anchor did not read back after embedding")

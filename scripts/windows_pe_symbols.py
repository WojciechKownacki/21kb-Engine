"""Read Windows PE debug identities and file PDBs into a symbol store layout."""

from __future__ import annotations

import os
import shutil
import struct
import tempfile
from dataclasses import dataclass
from pathlib import Path, PureWindowsPath
from typing import Iterable


class WindowsSymbolError(RuntimeError):
    pass


_IMAGE_DEBUG_TYPE_CODEVIEW = 2
_MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"


@dataclass(frozen=True)
class PdbIdentity:
    """The PDB a linked image names in its CodeView record."""

    name: str
    guid: bytes
    age: int
    linked_path: str

    @property
    def identifier(self) -> str:
        """Symbol store directory: the GUID as 32 upper-case hex digits, then the age in hex."""
        data1, data2, data3 = struct.unpack_from("<IHH", self.guid)
        return f"{data1:08X}{data2:04X}{data3:04X}{self.guid[8:].hex().upper()}{self.age:X}"


def _u16(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 2 > len(data):
        raise WindowsSymbolError("PE image is truncated")
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(data):
        raise WindowsSymbolError("PE image is truncated")
    return struct.unpack_from("<I", data, offset)[0]


def pe_pdb_identity(path: Path) -> PdbIdentity | None:
    """The CodeView (RSDS) identity of a PE file, or None when it was linked without one."""
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise WindowsSymbolError(f"not a PE image: {path.name}")
    nt = _u32(data, 0x3C)
    if data[nt:nt + 4] != b"PE\0\0":
        raise WindowsSymbolError(f"PE signature is missing: {path.name}")
    file_header = nt + 4
    section_count = _u16(data, file_header + 2)
    optional_size = _u16(data, file_header + 16)
    optional = file_header + 20
    magic = _u16(data, optional)
    if magic == 0x20B:
        directories = optional + 112
    elif magic == 0x10B:
        directories = optional + 96
    else:
        raise WindowsSymbolError(f"PE optional header is unknown: {path.name}")
    directory_count = _u32(data, directories - 4)
    if directory_count <= 6:
        return None
    debug_rva = _u32(data, directories + 6 * 8)
    debug_size = _u32(data, directories + 6 * 8 + 4)
    if debug_rva == 0 or debug_size == 0:
        return None
    sections = optional + optional_size
    debug_offset = None
    for index in range(section_count):
        header = sections + index * 40
        virtual_size = _u32(data, header + 8)
        virtual_address = _u32(data, header + 12)
        raw_size = _u32(data, header + 16)
        raw_pointer = _u32(data, header + 20)
        if virtual_address <= debug_rva < virtual_address + max(virtual_size, raw_size):
            debug_offset = raw_pointer + (debug_rva - virtual_address)
            break
    if debug_offset is None:
        raise WindowsSymbolError(f"PE debug directory is outside every section: {path.name}")
    for entry in range(debug_size // 28):
        record = debug_offset + entry * 28
        entry_type = _u32(data, record + 12)
        size = _u32(data, record + 16)
        pointer = _u32(data, record + 24)
        if entry_type != _IMAGE_DEBUG_TYPE_CODEVIEW or size < 24 or pointer + size > len(data):
            continue
        if data[pointer:pointer + 4] != b"RSDS":
            continue
        guid = bytes(data[pointer + 4:pointer + 20])
        age = _u32(data, pointer + 20)
        raw_name = data[pointer + 24:pointer + size].split(b"\0", 1)[0]
        linked_path = raw_name.decode("utf-8", errors="replace")
        name = PureWindowsPath(linked_path).name
        if not name or name in (".", "..") or not name.lower().endswith(".pdb"):
            raise WindowsSymbolError(f"PE names an unusable PDB: {path.name}")
        return PdbIdentity(name=name, guid=guid, age=age, linked_path=linked_path)
    return None


def pdb_guid(path: Path) -> bytes | None:
    """The GUID in a PDB's information stream, or None when the file is not an MSF 7 PDB."""
    with path.open("rb") as stream:
        header = stream.read(56)
        if len(header) < 56 or not header.startswith(_MSF_MAGIC):
            return None
        block_size, _free_map, block_count, directory_bytes, _reserved, block_map = struct.unpack_from(
            "<6I", header, len(_MSF_MAGIC)
        )
        if block_size not in (512, 1024, 2048, 4096) or directory_bytes == 0 or block_map >= block_count:
            return None

        def read_blocks(blocks: Iterable[int], size: int) -> bytes:
            chunks = []
            for block in blocks:
                if block >= block_count:
                    raise WindowsSymbolError(f"PDB block is out of range: {path.name}")
                stream.seek(block * block_size)
                chunks.append(stream.read(block_size))
            return b"".join(chunks)[:size]

        directory_block_count = (directory_bytes + block_size - 1) // block_size
        stream.seek(block_map * block_size)
        map_bytes = stream.read(directory_block_count * 4)
        if len(map_bytes) != directory_block_count * 4:
            return None
        directory = read_blocks(struct.unpack(f"<{directory_block_count}I", map_bytes), directory_bytes)
        stream_count = _u32(directory, 0)
        if stream_count < 2 or 4 + stream_count * 4 > len(directory):
            return None
        sizes = struct.unpack_from(f"<{stream_count}I", directory, 4)
        cursor = 4 + stream_count * 4
        # Streams before the information stream only need their block lists skipped.
        first_size = 0 if sizes[0] == 0xFFFFFFFF else sizes[0]
        cursor += ((first_size + block_size - 1) // block_size) * 4
        info_size = sizes[1]
        if info_size == 0xFFFFFFFF or info_size < 28:
            return None
        info_block_count = (info_size + block_size - 1) // block_size
        if cursor + info_block_count * 4 > len(directory):
            return None
        info = read_blocks(struct.unpack_from(f"<{info_block_count}I", directory, cursor), info_size)
        return bytes(info[12:28])


def _same_tree_child(root: Path, child: Path) -> bool:
    try:
        child.resolve(strict=False).relative_to(root.resolve(strict=False))
    except ValueError:
        return False
    return True


def find_matching_pdb(identity: PdbIdentity, candidates: Iterable[Path]) -> Path | None:
    """The first candidate PDB whose GUID is the image's; a PDB from another build is never used."""
    for candidate in candidates:
        if candidate.is_file() and pdb_guid(candidate) == identity.guid:
            return candidate.resolve(strict=True)
    return None


def symbol_store_path(store: Path, identity: PdbIdentity) -> Path:
    return store / identity.name / identity.identifier / identity.name


def publish_to_symbol_store(store: Path, symbols: Iterable[tuple[PdbIdentity, Path]]) -> list[Path]:
    """File each PDB as <store>/<name>.pdb/<GUID><age>/<name>.pdb, the layout symbol servers serve.

    An entry already present is left alone: the same identity is the same build.
    Each new entry appears atomically, so a reader never sees a partial PDB.
    """
    published: list[Path] = []
    store.mkdir(parents=True, exist_ok=True)
    for identity, pdb in symbols:
        destination = symbol_store_path(store, identity)
        if not _same_tree_child(store, destination):
            raise WindowsSymbolError(f"symbol store entry escapes the store: {identity.name}")
        published.append(destination)
        if destination.is_file() and pdb_guid(destination) == identity.guid:
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(prefix=".pdb-", dir=destination.parent)
        os.close(descriptor)
        temporary = Path(temporary_name)
        try:
            shutil.copyfile(pdb, temporary)
            os.replace(temporary, destination)
        finally:
            if temporary.exists():
                temporary.unlink()
    return published


def is_windows_pe(path: Path) -> bool:
    return path.suffix.lower() in (".exe", ".dll") and path.is_file()

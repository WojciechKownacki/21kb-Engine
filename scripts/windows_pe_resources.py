"""Apply package-selected icon and version metadata to a copied Windows player."""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import os
import re
import struct
from pathlib import Path


class WindowsResourceError(RuntimeError):
    pass


def _aligned(data: bytes) -> bytes:
    return data + (b"\0" * ((-len(data)) % 4))


def _wide(text: str) -> bytes:
    return text.encode("utf-16le") + b"\0\0"


def _block(key: str, value: bytes, value_length: int, value_type: int, children: tuple[bytes, ...] = ()) -> bytes:
    data = struct.pack("<HHH", 0, value_length, value_type) + _wide(key)
    data = _aligned(data) + value
    if children:
        data = _aligned(data)
        for child in children:
            data += child
            data = _aligned(data)
    if len(data) > 0xFFFF:
        raise WindowsResourceError(f"version resource block is too large: {key}")
    return struct.pack("<H", len(data)) + data[2:]


def _version_tuple(version: str) -> tuple[int, int, int, int]:
    match = re.fullmatch(r"(\d+)(?:\.(\d+))?(?:\.(\d+))?(?:\.(\d+))?(?:[-+][0-9A-Za-z.-]+)?", version)
    if match is None:
        raise WindowsResourceError("Windows version must begin with one to four numeric components")
    values = tuple(int(value or "0") for value in match.groups(default="0"))
    if any(value > 65535 for value in values):
        raise WindowsResourceError("Windows version components must not exceed 65535")
    return values  # type: ignore[return-value]


def _version_resource(
    *,
    product_name: str,
    publisher: str,
    version: str,
    executable_name: str,
    development: bool,
) -> bytes:
    major, minor, patch, build = _version_tuple(version)
    version_ms = (major << 16) | minor
    version_ls = (patch << 16) | build
    fixed = struct.pack(
        "<13I",
        0xFEEF04BD,
        0x00010000,
        version_ms,
        version_ls,
        version_ms,
        version_ls,
        0x0000003F,
        0x00000001 if development else 0,
        0x00040004,
        0x00000001,
        0,
        0,
        0,
    )

    def string_value(key: str, value: str) -> bytes:
        encoded = _wide(value)
        return _block(key, encoded, len(encoded) // 2, 1)

    strings = (
        string_value("CompanyName", publisher),
        string_value("FileDescription", product_name),
        string_value("FileVersion", version),
        string_value("InternalName", executable_name),
        string_value("OriginalFilename", executable_name + ".exe"),
        string_value("ProductName", product_name),
        string_value("ProductVersion", version),
    )
    string_table = _block("040904B0", b"", 0, 1, strings)
    string_file_info = _block("StringFileInfo", b"", 0, 1, (string_table,))
    translation = _block("Translation", struct.pack("<HH", 0x0409, 1200), 4, 0)
    var_file_info = _block("VarFileInfo", b"", 0, 1, (translation,))
    return _block("VS_VERSION_INFO", fixed, len(fixed), 0, (string_file_info, var_file_info))


def _icon_resources(path: Path) -> tuple[list[bytes], bytes]:
    data = path.read_bytes()
    if path.suffix.casefold() == ".png":
        if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
            raise WindowsResourceError("Windows PNG icon has an invalid header")
        width, height = struct.unpack_from(">II", data, 16)
        if not 1 <= width <= 4096 or not 1 <= height <= 4096:
            raise WindowsResourceError("Windows PNG icon dimensions must be between 1 and 4096 pixels")
        entry = struct.pack(
            "<BBBBHHIH", width if width < 256 else 0, height if height < 256 else 0,
            0, 0, 1, 32, len(data), 1
        )
        return [data], struct.pack("<HHH", 0, 1, 1) + entry
    if len(data) < 6:
        raise WindowsResourceError("Windows icon file is truncated")
    reserved, image_type, count = struct.unpack_from("<HHH", data, 0)
    if reserved != 0 or image_type != 1 or count == 0 or count > 64 or len(data) < 6 + count * 16:
        raise WindowsResourceError("Windows icon file has an invalid directory")
    images: list[bytes] = []
    group_entries: list[bytes] = []
    for index in range(count):
        offset = 6 + index * 16
        width, height, colors, entry_reserved, planes, bits, size, image_offset = struct.unpack_from(
            "<BBBBHHII", data, offset
        )
        if entry_reserved != 0 or size == 0 or image_offset < 6 + count * 16 or image_offset + size > len(data):
            raise WindowsResourceError("Windows icon contains an invalid image entry")
        images.append(data[image_offset:image_offset + size])
        group_entries.append(struct.pack(
            "<BBBBHHIH", width, height, colors, 0, planes, bits, size, index + 1
        ))
    return images, struct.pack("<HHH", 0, 1, count) + b"".join(group_entries)


def _integer_resource(identifier: int) -> wintypes.LPCWSTR:
    return ctypes.cast(ctypes.c_void_p(identifier), wintypes.LPCWSTR)


def apply_windows_resources(
    executable: Path,
    *,
    product_name: str,
    publisher: str,
    version: str,
    executable_name: str,
    development: bool,
    icon: Path | None,
) -> None:
    if os.name != "nt":
        raise WindowsResourceError("Windows PE resources can only be written on Windows")
    if not executable.is_file() or executable.read_bytes()[:2] != b"MZ":
        raise WindowsResourceError("Windows player is not a PE executable")
    if icon is not None and icon.suffix.casefold() not in (".ico", ".png"):
        raise WindowsResourceError("Windows application icon must be an .ico or PNG file")
    version_data = _version_resource(
        product_name=product_name,
        publisher=publisher,
        version=version,
        executable_name=executable_name,
        development=development,
    )
    icon_images: list[bytes] = []
    icon_group = b""
    if icon is not None:
        icon_images, icon_group = _icon_resources(icon)

    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    kernel32.BeginUpdateResourceW.restype = wintypes.HANDLE
    kernel32.UpdateResourceW.argtypes = [
        wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR,
        wintypes.WORD, wintypes.LPVOID, wintypes.DWORD,
    ]
    kernel32.UpdateResourceW.restype = wintypes.BOOL
    kernel32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    kernel32.EndUpdateResourceW.restype = wintypes.BOOL
    handle = kernel32.BeginUpdateResourceW(str(executable), False)
    if not handle:
        raise WindowsResourceError(f"BeginUpdateResourceW failed with error {ctypes.get_last_error()}")
    committed = False
    buffers: list[ctypes.Array[ctypes.c_char]] = []
    try:
        def update(resource_type: int, identifier: int, language: int, value: bytes) -> None:
            buffer = ctypes.create_string_buffer(value)
            buffers.append(buffer)
            if not kernel32.UpdateResourceW(
                handle,
                _integer_resource(resource_type),
                _integer_resource(identifier),
                language,
                ctypes.cast(buffer, wintypes.LPVOID),
                len(value),
            ):
                raise WindowsResourceError(
                    f"UpdateResourceW failed with error {ctypes.get_last_error()}"
                )

        update(16, 1, 0x0409, version_data)
        if icon_images:
            for index, image in enumerate(icon_images, start=1):
                update(3, index, 0x0409, image)
            update(14, 1, 0x0409, icon_group)
        if not kernel32.EndUpdateResourceW(handle, False):
            raise WindowsResourceError(f"EndUpdateResourceW failed with error {ctypes.get_last_error()}")
        committed = True
    finally:
        if not committed:
            kernel32.EndUpdateResourceW(handle, True)

"""Authenticode signing and verification of the Windows images a package ships."""

from __future__ import annotations

import ctypes
import os
import re
from ctypes import wintypes
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


class AuthenticodeError(RuntimeError):
    pass


_THUMBPRINT = re.compile(r"[0-9A-Fa-f]{40}\Z")

# WinVerifyTrust results that matter here.
TRUST_SUCCESS = 0
TRUST_E_NOSIGNATURE = 0x800B0100
TRUST_E_BAD_DIGEST = 0x80096010
CERT_E_UNTRUSTEDROOT = 0x800B0109
CERT_E_CHAINING = 0x800B010A
_UNTRUSTED_CHAIN = (CERT_E_UNTRUSTEDROOT, CERT_E_CHAINING)


def normalize_thumbprint(value: str) -> str:
    text = value.replace(" ", "").replace(":", "").strip()
    if not _THUMBPRINT.fullmatch(text):
        raise AuthenticodeError("certificate thumbprint must be 40 hexadecimal digits (SHA-1)")
    return text.upper()


def _version_key(path: Path) -> tuple[int, ...]:
    try:
        return tuple(int(part) for part in path.parent.parent.name.split("."))
    except ValueError:
        return (0,)


def find_signtool() -> Path:
    """The newest x64 signtool.exe of an installed Windows 10/11 SDK."""
    roots: list[Path] = []
    if os.name == "nt":
        try:
            import winreg

            with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Windows Kits\Installed Roots") as key:
                roots.append(Path(winreg.QueryValueEx(key, "KitsRoot10")[0]) / "bin")
        except OSError:
            pass
    for variable in ("ProgramFiles(x86)", "ProgramFiles"):
        value = os.environ.get(variable)
        if value:
            roots.append(Path(value) / "Windows Kits" / "10" / "bin")
    candidates: list[Path] = []
    for root in roots:
        candidates.extend(sorted(root.glob("10.*/x64/signtool.exe"), key=_version_key, reverse=True))
        candidates.append(root / "x64" / "signtool.exe")
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    raise AuthenticodeError("signtool.exe was not found; install the Windows SDK signing tools")


def signtool_sign_command(
    signtool: Path, files: Sequence[Path], thumbprint: str, timestamp_url: str | None
) -> list[Path | str]:
    """signtool arguments for a certificate in the current user's personal store.

    The store holds the key, so nothing secret is on the command line.
    """
    command: list[Path | str] = [
        signtool, "sign", "/fd", "SHA256", "/s", "My", "/sha1", normalize_thumbprint(thumbprint),
    ]
    if timestamp_url:
        command.extend(("/tr", timestamp_url, "/td", "SHA256"))
    command.extend(files)
    return command


@dataclass(frozen=True)
class AuthenticodeSignature:
    status: int
    thumbprint: str | None
    subject: str | None

    @property
    def intact(self) -> bool:
        """Signed, and the image still hashes to what was signed."""
        return self.status == TRUST_SUCCESS or self.status in _UNTRUSTED_CHAIN


class _GUID(ctypes.Structure):
    _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD), ("Data3", wintypes.WORD), ("Data4", ctypes.c_ubyte * 8)]


class _WINTRUST_FILE_INFO(ctypes.Structure):
    _fields_ = [
        ("cbStruct", wintypes.DWORD),
        ("pcwszFilePath", wintypes.LPCWSTR),
        ("hFile", wintypes.HANDLE),
        ("pgKnownSubject", ctypes.c_void_p),
    ]


class _WINTRUST_DATA(ctypes.Structure):
    _fields_ = [
        ("cbStruct", wintypes.DWORD),
        ("pPolicyCallbackData", ctypes.c_void_p),
        ("pSIPClientData", ctypes.c_void_p),
        ("dwUIChoice", wintypes.DWORD),
        ("fdwRevocationChecks", wintypes.DWORD),
        ("dwUnionChoice", wintypes.DWORD),
        ("pFile", ctypes.POINTER(_WINTRUST_FILE_INFO)),
        ("dwStateAction", wintypes.DWORD),
        ("hWVTStateData", wintypes.HANDLE),
        ("pwszURLReference", wintypes.LPCWSTR),
        ("dwProvFlags", wintypes.DWORD),
        ("dwUIContext", wintypes.DWORD),
        ("pSignatureSettings", ctypes.c_void_p),
    ]


class _CRYPT_PROVIDER_SGNR(ctypes.Structure):
    _fields_ = [
        ("cbStruct", wintypes.DWORD),
        ("sftVerifyAsOf", wintypes.FILETIME),
        ("csCertChain", wintypes.DWORD),
        ("pasCertChain", ctypes.c_void_p),
    ]


class _CRYPT_PROVIDER_CERT(ctypes.Structure):
    _fields_ = [("cbStruct", wintypes.DWORD), ("pCert", ctypes.c_void_p)]


_WINTRUST_ACTION_GENERIC_VERIFY_V2 = _GUID(
    0x00AAC56B, 0xCD44, 0x11D0, (ctypes.c_ubyte * 8)(0x8C, 0xC2, 0x00, 0xC0, 0x4F, 0xC2, 0x95, 0xEE)
)
_WTD_UI_NONE = 2
_WTD_REVOKE_NONE = 0
_WTD_CHOICE_FILE = 1
_WTD_STATEACTION_VERIFY = 1
_WTD_STATEACTION_CLOSE = 2
_WTD_CACHE_ONLY_URL_RETRIEVAL = 0x1000
_CERT_SHA1_HASH_PROP_ID = 3
_CERT_NAME_SIMPLE_DISPLAY_TYPE = 4


def read_signature(path: Path) -> AuthenticodeSignature:
    """WinVerifyTrust's verdict on an image and, when it carries one, its signer.

    Revocation is not checked and nothing is fetched from the network, so the
    answer depends only on the file and the machine's trusted roots.
    """
    if os.name != "nt":
        raise AuthenticodeError("Authenticode verification needs Windows")
    wintrust = ctypes.WinDLL("wintrust.dll")
    crypt32 = ctypes.WinDLL("crypt32.dll")
    wintrust.WinVerifyTrust.argtypes = [wintypes.HWND, ctypes.POINTER(_GUID), ctypes.c_void_p]
    wintrust.WinVerifyTrust.restype = wintypes.LONG
    wintrust.WTHelperProvDataFromStateData.argtypes = [wintypes.HANDLE]
    wintrust.WTHelperProvDataFromStateData.restype = ctypes.c_void_p
    wintrust.WTHelperGetProvSignerFromChain.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    wintrust.WTHelperGetProvSignerFromChain.restype = ctypes.POINTER(_CRYPT_PROVIDER_SGNR)
    crypt32.CertGetCertificateContextProperty.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]
    crypt32.CertGetCertificateContextProperty.restype = wintypes.BOOL
    crypt32.CertGetNameStringW.argtypes = [ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.LPWSTR, wintypes.DWORD]
    crypt32.CertGetNameStringW.restype = wintypes.DWORD

    file_info = _WINTRUST_FILE_INFO(ctypes.sizeof(_WINTRUST_FILE_INFO), str(path), None, None)
    data = _WINTRUST_DATA()
    data.cbStruct = ctypes.sizeof(_WINTRUST_DATA)
    data.dwUIChoice = _WTD_UI_NONE
    data.fdwRevocationChecks = _WTD_REVOKE_NONE
    data.dwUnionChoice = _WTD_CHOICE_FILE
    data.pFile = ctypes.pointer(file_info)
    data.dwStateAction = _WTD_STATEACTION_VERIFY
    data.dwProvFlags = _WTD_CACHE_ONLY_URL_RETRIEVAL
    action = _GUID.from_buffer_copy(_WINTRUST_ACTION_GENERIC_VERIFY_V2)
    status = wintrust.WinVerifyTrust(None, ctypes.byref(action), ctypes.byref(data)) & 0xFFFFFFFF
    thumbprint = None
    subject = None
    try:
        provider = wintrust.WTHelperProvDataFromStateData(data.hWVTStateData) if data.hWVTStateData else None
        signer = wintrust.WTHelperGetProvSignerFromChain(provider, 0, False, 0) if provider else None
        if signer and signer.contents.csCertChain > 0 and signer.contents.pasCertChain:
            leaf = _CRYPT_PROVIDER_CERT.from_address(signer.contents.pasCertChain)
            digest = (ctypes.c_ubyte * 20)()
            size = wintypes.DWORD(20)
            if crypt32.CertGetCertificateContextProperty(leaf.pCert, _CERT_SHA1_HASH_PROP_ID, digest, ctypes.byref(size)):
                thumbprint = bytes(digest[: size.value]).hex().upper()
            name = ctypes.create_unicode_buffer(512)
            if crypt32.CertGetNameStringW(leaf.pCert, _CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, None, name, 512) > 1:
                subject = name.value
    finally:
        data.dwStateAction = _WTD_STATEACTION_CLOSE
        wintrust.WinVerifyTrust(None, ctypes.byref(action), ctypes.byref(data))
    return AuthenticodeSignature(status, thumbprint, subject)


def verify_signed(
    files: Iterable[Path],
    *,
    expected_thumbprint: str | None,
    allow_untrusted_root: bool = False,
) -> list[AuthenticodeSignature]:
    """Every file must carry an intact signature by the expected certificate.

    A release also needs the chain to end in a root this machine trusts; only
    a test signing with a throwaway certificate may waive that.
    """
    expected = normalize_thumbprint(expected_thumbprint) if expected_thumbprint else None
    results: list[AuthenticodeSignature] = []
    for path in files:
        signature = read_signature(path)
        if signature.status == TRUST_E_NOSIGNATURE:
            raise AuthenticodeError(f"{path.name} is not signed")
        if not signature.intact:
            raise AuthenticodeError(f"{path.name} has an invalid signature (0x{signature.status:08X})")
        if signature.status != TRUST_SUCCESS and not allow_untrusted_root:
            raise AuthenticodeError(f"{path.name} is signed by a certificate this machine does not trust (0x{signature.status:08X})")
        if expected is not None and signature.thumbprint != expected:
            raise AuthenticodeError(f"{path.name} is signed by {signature.thumbprint}, not by {expected}")
        results.append(signature)
    return results

"""Exercise real failing test processes without exposing diagnostic windows to the user."""

import argparse
import ctypes
from ctypes import wintypes
import os
import subprocess
import tempfile
import time
import uuid


def run_probe(executable, mode):
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

    class StartupInfo(ctypes.Structure):
        _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR),
                    ("lpDesktop", wintypes.LPWSTR), ("lpTitle", wintypes.LPWSTR),
                    ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
                    ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD),
                    ("dwXCountChars", wintypes.DWORD), ("dwYCountChars", wintypes.DWORD),
                    ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                    ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD),
                    ("lpReserved2", ctypes.c_void_p), ("hStdInput", wintypes.HANDLE),
                    ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE)]

    class ProcessInfo(ctypes.Structure):
        _fields_ = [("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE),
                    ("dwProcessId", wintypes.DWORD), ("dwThreadId", wintypes.DWORD)]

    kernel32.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p,
                                      ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD,
                                      ctypes.c_void_p, wintypes.LPCWSTR,
                                      ctypes.POINTER(StartupInfo), ctypes.POINTER(ProcessInfo)]
    kernel32.CreateProcessW.restype = wintypes.BOOL
    kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel32.WaitForSingleObject.restype = wintypes.DWORD
    kernel32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
    kernel32.TerminateProcess.restype = wintypes.BOOL
    kernel32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel32.GetExitCodeProcess.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p,
                                     wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    user32.CreateDesktopW.restype = wintypes.HANDLE
    user32.CloseDesktop.argtypes = [wintypes.HANDLE]
    user32.CloseDesktop.restype = wintypes.BOOL
    user32.EnumDesktopWindows.argtypes = [wintypes.HANDLE, callback_type, wintypes.LPARAM]
    user32.EnumDesktopWindows.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    name = "21kb-test-errors-" + uuid.uuid4().hex
    desktop = user32.CreateDesktopW(name, None, None, 0, 0x10000000, None)
    if not desktop:
        raise ctypes.WinError(ctypes.get_last_error())
    process = ProcessInfo()
    window_found = False
    try:
        @callback_type
        def inspect_window(window, _):
            nonlocal window_found
            owner = wintypes.DWORD()
            user32.GetWindowThreadProcessId(window, ctypes.byref(owner))
            if owner.value == process.dwProcessId and user32.IsWindowVisible(window):
                window_found = True
            return True

        import msvcrt
        # STARTUPINFO.lpDesktop is not exposed by Python's subprocess wrapper. Use the native
        # API and files, so even the negative control can never appear on the active desktop.
        with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors, open(os.devnull, "rb") as stdin:
            handles = [msvcrt.get_osfhandle(file.fileno()) for file in (stdin, output, errors)]
            for handle in handles:
                os.set_handle_inheritable(handle, True)
            startup = StartupInfo(cb=ctypes.sizeof(StartupInfo), lpDesktop=name, dwFlags=0x101,
                                  hStdInput=handles[0], hStdOutput=handles[1], hStdError=handles[2])
            command = ctypes.create_unicode_buffer(subprocess.list2cmdline([executable, mode]))
            if not kernel32.CreateProcessW(executable, command, None, None, True,
                                          subprocess.CREATE_NO_WINDOW, None, None,
                                          ctypes.byref(startup), ctypes.byref(process)):
                raise ctypes.WinError(ctypes.get_last_error())
            kernel32.CloseHandle(process.hThread)
            deadline = time.monotonic() + 10
            while kernel32.WaitForSingleObject(process.hProcess, 0) == 0x102 and time.monotonic() < deadline:
                ctypes.set_last_error(0)
                if not user32.EnumDesktopWindows(desktop, inspect_window, 0) and ctypes.get_last_error():
                    raise ctypes.WinError(ctypes.get_last_error())
                if window_found:
                    kernel32.TerminateProcess(process.hProcess, 254)
                    break
                time.sleep(0.01)
            if kernel32.WaitForSingleObject(process.hProcess, 5000) != 0:
                raise AssertionError(f"{mode}: test process did not terminate automatically")
            code = wintypes.DWORD()
            if not kernel32.GetExitCodeProcess(process.hProcess, ctypes.byref(code)):
                raise ctypes.WinError(ctypes.get_last_error())
            output.seek(0)
            errors.seek(0)
            return code.value, output.read(), errors.read(), window_found
    finally:
        if process.hProcess:
            if kernel32.WaitForSingleObject(process.hProcess, 0) == 0x102:
                kernel32.TerminateProcess(process.hProcess, 254)
                kernel32.WaitForSingleObject(process.hProcess, 5000)
            kernel32.CloseHandle(process.hProcess)
        if not user32.CloseDesktop(desktop):
            raise ctypes.WinError(ctypes.get_last_error())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True)
    parser.add_argument("--debug-crt", type=int, choices=(0, 1), required=True)
    args = parser.parse_args()
    code, _, _, window = run_probe(args.probe, "dialog-control")
    assert window and code != 0, "negative control did not detect and terminate a private-desktop dialog"
    code, stdout, _, window = run_probe(args.probe, "capabilities")
    assert code == 0 and not window
    assert stdout.strip() == f"debug-crt={args.debug_crt}".encode(), "probe configuration does not match the test configuration"
    modes = ["abort", "assert", "global-assert"]
    if args.debug_crt:
        modes.append("crt-assert")
    for mode in modes:
        code, stdout, stderr, window = run_probe(args.probe, mode)
        assert code != 0 and not window, f"{mode}: failure was ignored or opened a dialog"
        assert b"process error probe entered" in stdout, f"{mode}: buffered output was lost on termination"
        if mode == "assert":
            assert b"standard assertion probe" in stderr
        if mode == "global-assert":
            assert b"global assertion probe" in stderr
        if mode == "crt-assert":
            assert b"CRT assertion probe" in stderr
        print(f"test_process_errors {mode}=failed_without_dialog diagnostics=preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

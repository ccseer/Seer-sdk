"""
Seer Named Pipe JSON-RPC 2.0 Client for Python
Requires only Python standard library (ctypes). No external pip packages needed.
Works on Windows 10/11 (x64).

Note: This sample performs synchronous request-response RPC. Clients that also call
`seer.events.subscribe` must pass FILE_FLAG_OVERLAPPED in CreateFileW and read on a
dedicated reader thread so synchronous ReadFile does not block WriteFile.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
import json
import os
import sys

PIPE_PREFIX = r"\\.\pipe\Seer.IPC.v2."
MAX_FRAME = 64 * 1024

# Server DACL omits FILE_CREATE_PIPE_INSTANCE (FILE_APPEND_DATA = 0x4), so
# GENERIC_WRITE (which expands to FILE_GENERIC_WRITE with 0x4) is rejected.
# Always request the explicit non-append read/write mask 0x0012018B.
DESIRED_ACCESS = 0x0012018B
OPEN_EXISTING = 3
SECURITY_SQOS_PRESENT = 0x00100000
SECURITY_IDENTIFICATION = 0x00010000
PIPE_READMODE_MESSAGE = 0x00000002

INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value
ERROR_MORE_DATA = 234

SE_KERNEL_OBJECT = 6
OWNER_SECURITY_INFORMATION = 0x00000001
TOKEN_QUERY = 0x0008
TokenUser = 1
WinLocalSystemSid = 22
WinBuiltinAdministratorsSid = 26


class TOKEN_USER(ctypes.Structure):
    _fields_ = [
        ("UserSid", ctypes.c_void_p),
        ("Attributes", wintypes.DWORD),
    ]


class WNDCLASSEXW(ctypes.Structure):
    _fields_ = [
        ("cbSize", wintypes.UINT),
        ("style", wintypes.UINT),
        ("lpfnWndProc", ctypes.c_void_p),
        ("cbClsExtra", ctypes.c_int),
        ("cbWndExtra", ctypes.c_int),
        ("hInstance", wintypes.HINSTANCE),
        ("hIcon", wintypes.HICON),
        ("hCursor", wintypes.HANDLE),
        ("hbrBackground", wintypes.HBRUSH),
        ("lpszMenuName", wintypes.LPCWSTR),
        ("lpszClassName", wintypes.LPCWSTR),
        ("hIconSm", wintypes.HICON),
    ]


kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)

kernel32.GetCurrentProcessId.restype = wintypes.DWORD
kernel32.GetCurrentProcess.restype = wintypes.HANDLE
kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
kernel32.GetModuleHandleW.restype = wintypes.HMODULE

kernel32.ProcessIdToSessionId.argtypes = [wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
kernel32.ProcessIdToSessionId.restype = wintypes.BOOL

kernel32.GetNamedPipeServerSessionId.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
kernel32.GetNamedPipeServerSessionId.restype = wintypes.BOOL

kernel32.CreateFileW.argtypes = [
    wintypes.LPCWSTR,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.HANDLE,
]
kernel32.CreateFileW.restype = wintypes.HANDLE

kernel32.SetNamedPipeHandleState.argtypes = [
    wintypes.HANDLE,
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
    ctypes.POINTER(wintypes.DWORD),
]
kernel32.SetNamedPipeHandleState.restype = wintypes.BOOL

kernel32.WriteFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPCVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.WriteFile.restype = wintypes.BOOL

kernel32.ReadFile.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
    wintypes.LPVOID,
]
kernel32.ReadFile.restype = wintypes.BOOL

kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

kernel32.LocalFree.argtypes = [wintypes.HLOCAL]
kernel32.LocalFree.restype = wintypes.HLOCAL

advapi32.GetSecurityInfo.argtypes = [
    wintypes.HANDLE,
    ctypes.c_int,
    wintypes.DWORD,
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_void_p),
]
advapi32.GetSecurityInfo.restype = wintypes.DWORD

advapi32.OpenProcessToken.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.POINTER(wintypes.HANDLE)]
advapi32.OpenProcessToken.restype = wintypes.BOOL

advapi32.GetTokenInformation.argtypes = [
    wintypes.HANDLE,
    ctypes.c_int,
    wintypes.LPVOID,
    wintypes.DWORD,
    ctypes.POINTER(wintypes.DWORD),
]
advapi32.GetTokenInformation.restype = wintypes.BOOL

advapi32.CreateWellKnownSid.argtypes = [
    ctypes.c_int,
    ctypes.c_void_p,
    ctypes.c_void_p,
    ctypes.POINTER(wintypes.DWORD),
]
advapi32.CreateWellKnownSid.restype = wintypes.BOOL

advapi32.EqualSid.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
advapi32.EqualSid.restype = wintypes.BOOL

WNDPROC = ctypes.WINFUNCTYPE(ctypes.c_ssize_t, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM)
user32.DefWindowProcW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
user32.DefWindowProcW.restype = ctypes.c_ssize_t
user32.RegisterClassExW.argtypes = [ctypes.POINTER(WNDCLASSEXW)]
user32.RegisterClassExW.restype = wintypes.ATOM
user32.CreateWindowExW.argtypes = [
    wintypes.DWORD, wintypes.LPCWSTR, wintypes.LPCWSTR, wintypes.DWORD,
    ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
    wintypes.HWND, wintypes.HMENU, wintypes.HINSTANCE, wintypes.LPVOID,
]
user32.CreateWindowExW.restype = wintypes.HWND
user32.DestroyWindow.argtypes = [wintypes.HWND]
user32.DestroyWindow.restype = wintypes.BOOL


def current_session_id() -> int:
    sid = wintypes.DWORD()
    pid = kernel32.GetCurrentProcessId()
    if not kernel32.ProcessIdToSessionId(pid, ctypes.byref(sid)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sid.value


def _query_current_user_sid() -> tuple[ctypes.Array[ctypes.c_ubyte], ctypes.c_void_p]:
    token = wintypes.HANDLE()
    if not advapi32.OpenProcessToken(kernel32.GetCurrentProcess(), TOKEN_QUERY, ctypes.byref(token)):
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        needed = wintypes.DWORD(0)
        advapi32.GetTokenInformation(token, TokenUser, None, 0, ctypes.byref(needed))
        buf = (ctypes.c_ubyte * needed.value)()
        if not advapi32.GetTokenInformation(token, TokenUser, buf, needed, ctypes.byref(needed)):
            raise ctypes.WinError(ctypes.get_last_error())
        user = TOKEN_USER.from_buffer(buf)
        return buf, user.UserSid  # type: ignore[return-value]
    finally:
        kernel32.CloseHandle(token)


def _well_known_sid(sid_type: int) -> ctypes.Array[ctypes.c_ubyte]:
    size = wintypes.DWORD(68)
    buf = (ctypes.c_ubyte * size.value)()
    if not advapi32.CreateWellKnownSid(sid_type, None, buf, ctypes.byref(size)):
        raise ctypes.WinError(ctypes.get_last_error())
    return buf


def verify_pipe_server(pipe_handle: int, expected_session_id: int) -> None:
    server_sid = wintypes.DWORD()
    if not kernel32.GetNamedPipeServerSessionId(pipe_handle, ctypes.byref(server_sid)):
        raise ctypes.WinError(ctypes.get_last_error())
    if server_sid.value != expected_session_id:
        raise RuntimeError(
            f"pipe server session mismatch: got {server_sid.value}, expected {expected_session_id}"
        )

    owner_sid = ctypes.c_void_p()
    sd = ctypes.c_void_p()
    err = advapi32.GetSecurityInfo(
        pipe_handle,
        SE_KERNEL_OBJECT,
        OWNER_SECURITY_INFORMATION,
        ctypes.byref(owner_sid),
        None,
        None,
        None,
        ctypes.byref(sd),
    )
    if err != 0:
        raise ctypes.WinError(err)

    try:
        user_buf, user_sid_ptr = _query_current_user_sid()
        admin_buf = _well_known_sid(WinBuiltinAdministratorsSid)
        system_buf = _well_known_sid(WinLocalSystemSid)
        _keep_alive = (user_buf, admin_buf, system_buf)

        if not (
            advapi32.EqualSid(owner_sid, user_sid_ptr)
            or advapi32.EqualSid(owner_sid, admin_buf)
            or advapi32.EqualSid(owner_sid, system_buf)
        ):
            raise RuntimeError("untrusted pipe server owner SID")
    finally:
        if sd.value:
            kernel32.LocalFree(sd)


def connect_seer_pipe(pipe_name: str, expected_session_id: int) -> int:
    handle = kernel32.CreateFileW(
        pipe_name,
        DESIRED_ACCESS,
        0,
        None,
        OPEN_EXISTING,
        SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION,
        None,
    )
    if handle == INVALID_HANDLE_VALUE or not handle:
        raise ctypes.WinError(ctypes.get_last_error())

    try:
        verify_pipe_server(handle, expected_session_id)
        mode = wintypes.DWORD(PIPE_READMODE_MESSAGE)
        if not kernel32.SetNamedPipeHandleState(handle, ctypes.byref(mode), None, None):
            raise ctypes.WinError(ctypes.get_last_error())
    except Exception:
        kernel32.CloseHandle(handle)
        raise
    return handle


def send_frame(handle: int, payload: dict) -> None:
    raw = (json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n").encode("utf-8")
    written = wintypes.DWORD(0)
    if not kernel32.WriteFile(handle, raw, len(raw), ctypes.byref(written), None):
        raise ctypes.WinError(ctypes.get_last_error())
    if written.value != len(raw):
        raise RuntimeError(f"short pipe write: {written.value}/{len(raw)}")


def recv_frame(handle: int) -> dict:
    buf = ctypes.create_string_buffer(MAX_FRAME)
    chunks: list[bytes] = []
    while True:
        read = wintypes.DWORD(0)
        ok = kernel32.ReadFile(handle, buf, MAX_FRAME, ctypes.byref(read), None)
        chunks.append(buf.raw[: read.value])
        if ok:
            break
        err = ctypes.get_last_error()
        if err == ERROR_MORE_DATA:
            if sum(len(c) for c in chunks) > MAX_FRAME:
                raise RuntimeError("frame exceeds limit")
            continue
        raise ctypes.WinError(err)

    line = b"".join(chunks).decode("utf-8").strip()
    return json.loads(line)


def create_temp_top_window() -> tuple[int, object]:
    wndproc = WNDPROC(lambda hwnd, msg, wp, lp: user32.DefWindowProcW(hwnd, msg, wp, lp))
    cls_name = "SeerSdkPythonTestWindow"
    hinst = kernel32.GetModuleHandleW(None)
    wc = WNDCLASSEXW()
    wc.cbSize = ctypes.sizeof(WNDCLASSEXW)
    wc.lpfnWndProc = ctypes.cast(wndproc, ctypes.c_void_p)
    wc.hInstance = hinst
    wc.lpszClassName = cls_name
    user32.RegisterClassExW(ctypes.byref(wc))
    hwnd = user32.CreateWindowExW(0, cls_name, "SeerSdkTest", 0, 0, 0, 0, 0, None, None, hinst, None)
    if not hwnd:
        raise ctypes.WinError(ctypes.get_last_error())
    return hwnd, wndproc


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Seer IPC Named Pipe client example")
    parser.add_argument("--pipe-name", help="Override pipe name (defaults to Seer.IPC.v2.<SessionId>)")
    parser.add_argument("--preview", help="Open file/folder preview")
    parser.add_argument("--mode", default="main", choices=["main", "separated", "pinned", "standalone"])
    parser.add_argument("--list", action="store_true", help="Call seer.preview.list")
    parser.add_argument("--close", action="store_true", help="Call seer.preview.close")
    parser.add_argument("--hwnd", type=lambda s: int(s, 0), help="Existing top-level HWND owned by this process")
    parser.add_argument(
        "--test-register",
        action="store_true",
        help="Create a temporary top-level window and call seer.explorer.register + unregister",
    )
    args = parser.parse_args(argv)

    session_id = current_session_id()
    pipe_name = args.pipe_name or (PIPE_PREFIX + str(session_id))
    handle = connect_seer_pipe(pipe_name, session_id)

    temp_hwnd = None
    _wndproc_ref = None
    try:
        send_frame(handle, {"jsonrpc": "2.0", "id": 1, "method": "seer.getInfo", "params": {}})
        print("seer.getInfo:", json.dumps(recv_frame(handle), ensure_ascii=False))

        if args.list:
            send_frame(handle, {"jsonrpc": "2.0", "id": 2, "method": "seer.preview.list", "params": {}})
            print("seer.preview.list:", json.dumps(recv_frame(handle), ensure_ascii=False))

        if args.preview:
            send_frame(
                handle,
                {
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "seer.preview.open",
                    "params": {"path": os.path.abspath(args.preview), "mode": args.mode},
                },
            )
            print("seer.preview.open:", json.dumps(recv_frame(handle), ensure_ascii=False))

        if args.close:
            send_frame(handle, {"jsonrpc": "2.0", "id": 4, "method": "seer.preview.close", "params": {}})
            print("seer.preview.close:", json.dumps(recv_frame(handle), ensure_ascii=False))

        target_hwnd = args.hwnd
        if args.test_register:
            temp_hwnd, _wndproc_ref = create_temp_top_window()
            target_hwnd = temp_hwnd

        if target_hwnd:
            send_frame(
                handle,
                {
                    "jsonrpc": "2.0",
                    "id": 5,
                    "method": "seer.explorer.register",
                    "params": {"topHwnd": target_hwnd & 0xFFFFFFFF, "name": "CustomFileManager"},
                },
            )
            print("seer.explorer.register:", json.dumps(recv_frame(handle), ensure_ascii=False))

            if args.test_register:
                send_frame(
                    handle,
                    {
                        "jsonrpc": "2.0",
                        "id": 6,
                        "method": "seer.explorer.unregister",
                        "params": {"topHwnd": target_hwnd & 0xFFFFFFFF},
                    },
                )
                print("seer.explorer.unregister:", json.dumps(recv_frame(handle), ensure_ascii=False))
    finally:
        if temp_hwnd:
            user32.DestroyWindow(temp_hwnd)
        kernel32.CloseHandle(handle)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

"""
Seer IPC Client for Python
Requires only Python standard library (ctypes). No external pip packages needed.
Works on Windows.
"""

import sys
import os
import ctypes
from ctypes import wintypes
from enum import IntEnum

SEER_CLASS_NAME = "SeerWindowClass"
WM_COPYDATA = 0x004A


class SeerCommand(IntEnum):
    REQUEST_PATH = 4000
    RESPONSE_PATH = 4001
    INVOKE_W32 = 5000
    INVOKE_W32_SEP = 5001
    IS_VISIBLE = 5004
    HIDE = 5005
    GET_CURRENT_FILE = 5006
    GET_VERSION = 5007
    IS_TRACKING_ENABLED = 5008


class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [
        ("dwData", wintypes.LPARAM),
        ("cbData", wintypes.DWORD),
        ("lpData", ctypes.c_void_p),
    ]


# Win32 API functions setup
user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

FindWindowW = user32.FindWindowW
FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
FindWindowW.restype = wintypes.HWND

SendMessageW = user32.SendMessageW
SendMessageW.argtypes = [
    wintypes.HWND,
    wintypes.UINT,
    wintypes.WPARAM,
    ctypes.POINTER(COPYDATASTRUCT),
]
SendMessageW.restype = wintypes.LPARAM

GetCurrentProcessId = kernel32.GetCurrentProcessId
GetCurrentProcessId.restype = wintypes.DWORD

GetTickCount64 = kernel32.GetTickCount64
GetTickCount64.restype = ctypes.c_uint64

CreateFileMappingW = kernel32.CreateFileMappingW
CreateFileMappingW.argtypes = [
    wintypes.HANDLE,
    wintypes.LPVOID,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.LPCWSTR,
]
CreateFileMappingW.restype = wintypes.HANDLE

MapViewOfFile = kernel32.MapViewOfFile
MapViewOfFile.argtypes = [
    wintypes.HANDLE,
    wintypes.DWORD,
    wintypes.DWORD,
    wintypes.DWORD,
    ctypes.c_size_t,
]
MapViewOfFile.restype = wintypes.LPVOID

UnmapViewOfFile = kernel32.UnmapViewOfFile
UnmapViewOfFile.argtypes = [wintypes.LPCVOID]
UnmapViewOfFile.restype = wintypes.BOOL

CloseHandle = kernel32.CloseHandle
CloseHandle.argtypes = [wintypes.HANDLE]
CloseHandle.restype = wintypes.BOOL


def find_seer_window() -> int | None:
    """Finds the HWND of the running Seer window, or None if not running."""
    return FindWindowW(SEER_CLASS_NAME, None)


def is_running() -> bool:
    """Checks if Seer process window exists."""
    return bool(find_seer_window())


def send_simple_command(command: SeerCommand) -> int:
    """Sends a command with no payload to Seer."""
    hwnd = find_seer_window()
    if not hwnd:
        return 0

    cds = COPYDATASTRUCT()
    cds.dwData = int(command)
    cds.cbData = 0
    cds.lpData = None

    return SendMessageW(hwnd, WM_COPYDATA, 0, ctypes.byref(cds))


def send_path_command(command: SeerCommand, path: str) -> bool:
    """Sends a command with a unicode file path payload to Seer."""
    hwnd = find_seer_window()
    if not hwnd or not path:
        return False

    abs_path = os.path.abspath(path)
    buffer = ctypes.create_unicode_buffer(abs_path)
    cb_data = ctypes.sizeof(buffer)

    cds = COPYDATASTRUCT()
    cds.dwData = int(command)
    cds.cbData = cb_data
    cds.lpData = ctypes.cast(buffer, ctypes.c_void_p).value

    return bool(SendMessageW(hwnd, WM_COPYDATA, 0, ctypes.byref(cds)))


def preview_file(path: str) -> bool:
    """
    Invokes Seer preview for the given file path.
    Note: Consecutive calls should be at least 200ms apart.
    """
    return send_path_command(SeerCommand.INVOKE_W32, path)


def preview_file_separated(path: str) -> bool:
    """Invokes Seer preview in a separate window for the given file path."""
    return send_path_command(SeerCommand.INVOKE_W32_SEP, path)


def hide_preview() -> None:
    """Hides the Seer main window."""
    send_simple_command(SeerCommand.HIDE)


def is_main_window_visible() -> bool:
    """Checks whether Seer main window is currently visible."""
    return bool(send_simple_command(SeerCommand.IS_VISIBLE))


def is_tracking_enabled() -> bool:
    """Queries if file tracking mode is enabled in Seer."""
    return bool(send_simple_command(SeerCommand.IS_TRACKING_ENABLED))


def get_version() -> int:
    """Returns the Seer version integer (e.g. 40001 for version 4.0.1)."""
    return send_simple_command(SeerCommand.GET_VERSION)


def get_current_file() -> str | None:
    """
    Retrieves the path of the file currently previewed by Seer via shared memory.
    """
    hwnd = find_seer_window()
    if not hwnd:
        return None

    pid = GetCurrentProcessId()
    tick = GetTickCount64()
    map_name = f"SeerCurFile_{pid}_{tick}"
    max_path = 260
    buf_bytes = max_path * ctypes.sizeof(ctypes.c_wchar)

    INVALID_HANDLE_VALUE = wintypes.HANDLE(-1).value
    PAGE_READWRITE = 0x04
    FILE_MAP_ALL_ACCESS = 0xF001F

    h_map = CreateFileMappingW(
        INVALID_HANDLE_VALUE, None, PAGE_READWRITE, 0, buf_bytes, map_name
    )
    if not h_map:
        return None

    try:
        p_view = MapViewOfFile(h_map, FILE_MAP_ALL_ACCESS, 0, 0, buf_bytes)
        if not p_view:
            return None

        try:
            # Initialize buffer
            wchar_array = (ctypes.c_wchar * max_path).from_address(p_view)
            wchar_array[0] = "\0"

            map_name_buf = ctypes.create_unicode_buffer(map_name)
            cds = COPYDATASTRUCT()
            cds.dwData = int(SeerCommand.GET_CURRENT_FILE)
            cds.cbData = buf_bytes
            cds.lpData = ctypes.cast(map_name_buf, ctypes.c_void_p).value

            SendMessageW(hwnd, WM_COPYDATA, 0, ctypes.byref(cds))

            result = wchar_array.value
            return result if result else None
        finally:
            UnmapViewOfFile(p_view)
    finally:
        CloseHandle(h_map)


if __name__ == "__main__":
    print("=== Seer Python IPC Demo ===")
    if not is_running():
        print("Seer is not running! Please launch Seer first.")
        sys.exit(1)

    print(f"Seer Version: {get_version()}")
    print(f"Is Main Window Visible: {is_main_window_visible()}")
    print(f"Is Tracking Enabled: {is_tracking_enabled()}")

    if len(sys.argv) > 1:
        target_path = sys.argv[1]
        print(f"Previewing file: {target_path}")
        success = preview_file(target_path)
        print(f"Preview command sent: {success}")
    else:
        print("Usage: python seer_ipc.py <file_path_to_preview>")

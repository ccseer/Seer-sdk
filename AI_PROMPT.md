# Seer SDK - AI Integration & Context Guide (IPC)

> **Instructions for AI Assistants (ChatGPT, Claude, Gemini, Cursor, Copilot, etc.):**
> When a user asks to integrate an application with **Seer** (a QuickLook-like file preview tool for Windows) or control Seer via IPC, use the technical specifications, protocol definitions, and code templates in this document to generate accurate, robust code.
>
> *(Note: For developing custom in-process file preview renderer plugins, refer to the [Seer-Plugins](https://github.com/ccseer/Seer-Plugins) repository.)*

---

## 1. SDK Overview & Integration Modes

Seer SDK is a lightweight IPC library based on **pure Windows API** (`<Windows.h>`). It allows any programming language to interact with Seer without third-party framework dependencies.

It supports **two distinct IPC integration modes**:

| Mode | Use Case & Target Audience | Dependencies | Relevant Headers / Files |
| :--- | :--- | :--- | :--- |
| **1. Client-Initiated IPC** | Desktop apps, launchers, hotkey utilities, scripts triggering Seer previews or controlling the Seer window | **Pure Windows API** (`<Windows.h>`)<br>• Any language (C#, Python, C++, Rust, Go, PowerShell, AutoHotkey) | `seer/ipc.h`<br>`seer/ipchelper.h` |
| **2. File Manager Spacebar Integration** | Custom file managers or explorer clones wanting native Spacebar preview support | **Pure Windows API** (`<Windows.h>`)<br>• Explorer JSON configuration + `WM_COPYDATA` handshake | `%USERPROFILE%/Documents/Seer/explorers/<app>.json`<br>`seer/ipc.h` |

---

## 2. IPC Protocol Specification

### 2.1 Window Finding & Transport Protocol
- **Target Window Class**: `L"SeerWindowClass"`
- **Window Handle Lookup**: `HWND hwnd = FindWindowW(L"SeerWindowClass", nullptr);`
- **Transport Mechanism**: Win32 `SendMessageW(hwnd, WM_COPYDATA, 0, (LPARAM)&cds)` (`WM_COPYDATA = 0x004A`).
- **Path Resolution**: File paths **MUST be absolute canonical paths** (e.g. `C:\docs\report.pdf`). Seer runs in its own working directory and cannot resolve relative paths.
- **String Encoding**: UTF-16LE / Unicode (`wchar_t`). The `cbData` in `COPYDATASTRUCT` must be the byte count including the null terminator: `(wcslen(path) + 1) * sizeof(wchar_t)`.
- **Throttling Rule**: Consecutive preview invocations (`SEER_INVOKE_*`) must be throttled with an interval of at least **200ms**.

### 2.2 Command Enum Reference (`dwData`)

| Command Enum | Value | Payload (`lpData`, `cbData`) | Return Value / Description |
| :--- | :--- | :--- | :--- |
| `SEER_REQUEST_PATH` | `4000` | None | Sent from Seer to active explorer window asking for selected path |
| `SEER_RESPONSE_PATH` | `4001` | Null-terminated UTF-16 path string | Response sent from explorer back to Seer with file path |
| `SEER_INVOKE_W32` | `5000` | Null-terminated UTF-16 absolute file path | Triggers file preview in Seer main window |
| `SEER_INVOKE_W32_SEP` | `5001` | Null-terminated UTF-16 absolute file path | Triggers file preview in a separate standalone window |
| `SEER_IS_VISIBLE` | `5004` | None | Returns `1` if main preview window is visible, `0` otherwise |
| `SEER_HIDE` | `5005` | None | Closes/hides the main preview window |
| `SEER_GET_CURRENT_FILE`| `5006` | Null-terminated UTF-16 Shared Memory name | Fills shared memory buffer with path of currently previewed file |
| `SEER_GET_VERSION` | `5007` | None | Returns version integer (e.g. `40001` = v4.0.1) |
| `SEER_IS_TRACKING_ENABLED`| `5008`| None | Returns `1` if tracking mode is enabled, `0` otherwise |

### 2.3 `SEER_GET_CURRENT_FILE` Shared Memory Protocol
To retrieve the path of the file currently previewed by Seer:
1. Create a named file mapping:
   ```cpp
   wchar_t map_name[MAX_PATH];
   swprintf_s(map_name, L"SeerCurFile_%u_%llu", GetCurrentProcessId(), GetTickCount64());
   constexpr size_t buf_bytes = MAX_PATH * sizeof(wchar_t);
   HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, buf_bytes, map_name);
   ```
2. Map the view: `wchar_t* view = (wchar_t*)MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, buf_bytes);`
3. Initialize the buffer: `view[0] = L'\0';`
4. Send `WM_COPYDATA` to Seer:
   - `dwData = 5006` (`SEER_GET_CURRENT_FILE`)
   - `cbData = buf_bytes`
   - `lpData = map_name`
5. Seer writes the UTF-16 file path into the memory buffer.
6. Read the string from `view`, then `UnmapViewOfFile(view)` and `CloseHandle(map)`.

---

## 3. File Manager / Host Integration (Spacebar Preview)

When integrating a third-party file manager (or custom application with file lists) so that pressing `Space` triggers Seer preview:

### 3.1 Step 1: Register Explorer in Seer
Create a JSON file located at `%USERPROFILE%/Documents/Seer/explorers/<app_name>.json`:
```json
{
    "classname": "MyCustomFileManagerWindowClass",
    "windowtext": "",
    "appname": "MyCustomFileManager"
}
```
- `classname` (required): Win32 window class name of your file list window.
- `windowtext` (optional): Window title filter if needed.
- `appname` (optional): Human-readable application name.

### 3.2 Step 2: Handle `WM_COPYDATA` Handshake
When the user presses Space inside your registered window, Seer detects the keypress and sends `WM_COPYDATA` to your window:
1. When your window receives `WM_COPYDATA` with `cds->dwData == 4000` (`SEER_REQUEST_PATH`):
2. Retrieve the currently selected item's full absolute path.
3. Send `WM_COPYDATA` back to Seer (`FindWindowW(L"SeerWindowClass", nullptr)`):
   - `dwData = 4001` (`SEER_RESPONSE_PATH`)
   - `cbData = (path.length() + 1) * sizeof(wchar_t)`
   - `lpData = (LPVOID)path.c_str()`

```cpp
// Example Win32 WndProc handler in your file manager:
case WM_COPYDATA: {
    auto cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
    if (cds && cds->dwData == 4000) { // SEER_REQUEST_PATH
        std::wstring selectedPath = GetSelectedFilePath(); // App logic
        if (!selectedPath.empty()) {
            HWND seerHwnd = FindWindowW(L"SeerWindowClass", nullptr);
            if (seerHwnd) {
                COPYDATASTRUCT resp;
                resp.dwData = 4001; // SEER_RESPONSE_PATH
                resp.cbData = static_cast<DWORD>((selectedPath.length() + 1) * sizeof(wchar_t));
                resp.lpData = const_cast<wchar_t*>(selectedPath.c_str());
                SendMessageW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(hWnd), reinterpret_cast<LPARAM>(&resp));
            }
        }
        return TRUE;
    }
    break;
}
```

---

## 4. Multi-Language IPC Code Templates

### 4.1 C# (.NET 6+ / WinForms / WPF / WinUI)
```csharp
using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

public static class SeerClient
{
    private const string ClassName = "SeerWindowClass";
    private const int WmCopyData = 0x004A;

    [StructLayout(LayoutKind.Sequential)]
    private struct COPYDATASTRUCT
    {
        public IntPtr dwData;
        public int cbData;
        public IntPtr lpData;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr FindWindow(string? lpClassName, string? lpWindowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, ref COPYDATASTRUCT lParam);

    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentProcessId();

    [DllImport("kernel32.dll")]
    private static extern ulong GetTickCount64();

    public static IntPtr FindSeer() => FindWindow(ClassName, null);
    public static bool IsRunning() => FindSeer() != IntPtr.Zero;

    public static IntPtr SendSimple(int command)
    {
        IntPtr hwnd = FindSeer();
        if (hwnd == IntPtr.Zero) return IntPtr.Zero;
        var cds = new COPYDATASTRUCT { dwData = (IntPtr)command, cbData = 0, lpData = IntPtr.Zero };
        return SendMessage(hwnd, WmCopyData, IntPtr.Zero, ref cds);
    }

    public static bool SendPath(int command, string path)
    {
        IntPtr hwnd = FindSeer();
        if (hwnd == IntPtr.Zero || string.IsNullOrEmpty(path)) return false;

        string fullPath = Path.GetFullPath(path);
        IntPtr ptr = Marshal.StringToHGlobalUni(fullPath);
        try
        {
            var cds = new COPYDATASTRUCT
            {
                dwData = (IntPtr)command,
                cbData = (fullPath.Length + 1) * sizeof(char),
                lpData = ptr
            };
            return SendMessage(hwnd, WmCopyData, IntPtr.Zero, ref cds) != IntPtr.Zero;
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    public static bool Preview(string path) => SendPath(5000, path);
    public static bool PreviewSeparated(string path) => SendPath(5001, path);
    public static void Hide() => SendSimple(5005);
    public static bool IsVisible() => SendSimple(5004) != IntPtr.Zero;
    public static int GetVersion() => (int)SendSimple(5007);
    public static bool IsTrackingEnabled() => SendSimple(5008) != IntPtr.Zero;

    public static string? GetCurrentFile()
    {
        IntPtr hwnd = FindSeer();
        if (hwnd == IntPtr.Zero) return null;

        string mapName = $"SeerCurFile_{GetCurrentProcessId()}_{GetTickCount64()}";
        const int maxChars = 260;
        const int bufBytes = maxChars * sizeof(char);

        using var mmf = MemoryMappedFile.CreateNew(mapName, bufBytes);
        using var accessor = mmf.CreateViewAccessor(0, bufBytes);
        accessor.Write(0, '\0');

        IntPtr ptrMap = Marshal.StringToHGlobalUni(mapName);
        try
        {
            var cds = new COPYDATASTRUCT { dwData = (IntPtr)5006, cbData = bufBytes, lpData = ptrMap };
            SendMessage(hwnd, WmCopyData, IntPtr.Zero, ref cds);

            char[] buffer = new char[maxChars];
            accessor.ReadArray(0, buffer, 0, maxChars);
            int nullIdx = Array.IndexOf(buffer, '\0');
            string result = nullIdx >= 0 ? new string(buffer, 0, nullIdx) : new string(buffer);
            return string.IsNullOrEmpty(result) ? null : result;
        }
        finally
        {
            Marshal.FreeHGlobal(ptrMap);
        }
    }
}
```

### 4.2 Python (ctypes, Standard Library Only)
```python
import ctypes
from ctypes import wintypes
import os
import sys

SEER_CLASS_NAME = "SeerWindowClass"
WM_COPYDATA = 0x004A

class COPYDATASTRUCT(ctypes.Structure):
    _fields_ = [
        ("dwData", wintypes.LPARAM),
        ("cbData", wintypes.DWORD),
        ("lpData", ctypes.c_void_p),
    ]

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32

user32.FindWindowW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR]
user32.FindWindowW.restype = wintypes.HWND
user32.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, ctypes.POINTER(COPYDATASTRUCT)]
user32.SendMessageW.restype = wintypes.LPARAM

def find_seer() -> int | None:
    return user32.FindWindowW(SEER_CLASS_NAME, None)

def is_running() -> bool:
    return bool(find_seer())

def send_simple(command: int) -> int:
    hwnd = find_seer()
    if not hwnd:
        return 0
    cds = COPYDATASTRUCT(command, 0, None)
    return user32.SendMessageW(hwnd, WM_COPYDATA, 0, ctypes.byref(cds))

def send_path(command: int, path: str) -> bool:
    hwnd = find_seer()
    if not hwnd or not path:
        return False
    abs_path = os.path.abspath(path)
    buf = ctypes.create_unicode_buffer(abs_path)
    cds = COPYDATASTRUCT(command, ctypes.sizeof(buf), ctypes.cast(buf, ctypes.c_void_p).value)
    return bool(user32.SendMessageW(hwnd, WM_COPYDATA, 0, ctypes.byref(cds)))

def preview(path: str) -> bool:
    return send_path(5000, path)

def preview_separated(path: str) -> bool:
    return send_path(5001, path)

def hide() -> None:
    send_simple(5005)

def is_visible() -> bool:
    return bool(send_simple(5004))

def get_version() -> int:
    return send_simple(5007)

def is_tracking_enabled() -> bool:
    return bool(send_simple(5008))
```

### 4.3 Pure C++ (Win32, `<Windows.h>`)
```cpp
#include <windows.h>
#include <string>
#include <iostream>

class SeerClient {
public:
    static HWND findWindow() {
        return FindWindowW(L"SeerWindowClass", nullptr);
    }

    static bool isRunning() {
        return findWindow() != nullptr;
    }

    static LRESULT sendSimple(int command) {
        HWND hwnd = findWindow();
        if (!hwnd) return 0;
        COPYDATASTRUCT cds = { (ULONG_PTR)command, 0, nullptr };
        return SendMessageW(hwnd, WM_COPYDATA, 0, (LPARAM)&cds);
    }

    static bool sendPath(int command, const std::wstring& path) {
        HWND hwnd = findWindow();
        if (!hwnd || path.empty()) return false;

        wchar_t fullPath[MAX_PATH];
        GetFullPathNameW(path.c_str(), MAX_PATH, fullPath, nullptr);

        COPYDATASTRUCT cds;
        cds.dwData = (ULONG_PTR)command;
        cds.cbData = (DWORD)((wcslen(fullPath) + 1) * sizeof(wchar_t));
        cds.lpData = (PVOID)fullPath;
        return SendMessageW(hwnd, WM_COPYDATA, 0, (LPARAM)&cds) != 0;
    }

    static bool preview(const std::wstring& path) { return sendPath(5000, path); }
    static bool previewSeparated(const std::wstring& path) { return sendPath(5001, path); }
    static void hide() { sendSimple(5005); }
    static bool isVisible() { return sendSimple(5004) != 0; }
    static int getVersion() { return (int)sendSimple(5007); }
    static bool isTracking() { return sendSimple(5008) != 0; }
};
```

### 4.4 Rust (Raw Win32 FFI)
```rust
use std::env;
use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::Path;

const SEER_CLASS: &str = "SeerWindowClass";
const WM_COPYDATA: u32 = 0x004A;

#[repr(C)]
struct COPYDATASTRUCT {
    dw_data: usize,
    cb_data: u32,
    lp_data: *const std::ffi::c_void,
}

#[link(name = "user32")]
extern "system" {
    fn FindWindowW(class: *const u16, name: *const u16) -> *mut std::ffi::c_void;
    fn SendMessageW(hwnd: *mut std::ffi::c_void, msg: u32, wparam: usize, lparam: *const COPYDATASTRUCT) -> isize;
}

fn to_wide(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(Some(0)).collect()
}

pub struct Seer;

impl Seer {
    pub fn find_window() -> *mut std::ffi::c_void {
        unsafe { FindWindowW(to_wide(SEER_CLASS).as_ptr(), std::ptr::null()) }
    }

    pub fn is_running() -> bool {
        !Self::find_window().is_null()
    }

    pub fn send_simple(command: usize) -> isize {
        let hwnd = Self::find_window();
        if hwnd.is_null() { return 0; }
        let cds = COPYDATASTRUCT { dw_data: command, cb_data: 0, lp_data: std::ptr::null() };
        unsafe { SendMessageW(hwnd, WM_COPYDATA, 0, &cds) }
    }

    pub fn preview(path: &str) -> bool {
        let hwnd = Self::find_window();
        if hwnd.is_null() || path.is_empty() { return false; }

        let abs_path = match std::fs::canonicalize(Path::new(path)) {
            Ok(p) => p.to_string_lossy().into_owned().replace(r"\\?\", ""),
            Err(_) => path.to_string(),
        };

        let wide = to_wide(&abs_path);
        let cds = COPYDATASTRUCT {
            dw_data: 5000,
            cb_data: (wide.len() * 2) as u32,
            lp_data: wide.as_ptr() as *const std::ffi::c_void,
        };
        unsafe { SendMessageW(hwnd, WM_COPYDATA, 0, &cds) != 0 }
    }

    pub fn hide() { Self::send_simple(5005); }
    pub fn is_visible() -> bool { Self::send_simple(5004) != 0 }
    pub fn get_version() -> i32 { Self::send_simple(5007) as i32 }
}
```

### 4.5 Go (Golang, Standard Library `syscall`)
```go
package main

import (
	"path/filepath"
	"syscall"
	"unsafe"
)

type CopyDataStruct struct {
	DwData uintptr
	CbData uint32
	LpData uintptr
}

var (
	user32          = syscall.NewLazyDLL("user32.dll")
	procFindWindowW = user32.NewProc("FindWindowW")
	procSendMessage = user32.NewProc("SendMessageW")
)

func FindSeer() uintptr {
	cls, _ := syscall.UTF16PtrFromString("SeerWindowClass")
	hwnd, _, _ := procFindWindowW.Call(uintptr(unsafe.Pointer(cls)), 0)
	return hwnd
}

func SeerPreview(filePath string) bool {
	hwnd := FindSeer()
	if hwnd == 0 || filePath == "" {
		return false
	}
	abs, err := filepath.Abs(filePath)
	if err != nil {
		abs = filePath
	}
	utf16, _ := syscall.UTF16FromString(abs)
	cds := CopyDataStruct{
		DwData: 5000, // SEER_INVOKE_W32
		CbData: uint32(len(utf16) * 2),
		LpData: uintptr(unsafe.Pointer(&utf16[0])),
	}
	ret, _, _ := procSendMessage.Call(hwnd, 0x004A, 0, uintptr(unsafe.Pointer(&cds)))
	return ret != 0
}

func SeerHide() {
	hwnd := FindSeer()
	if hwnd == 0 { return }
	cds := CopyDataStruct{DwData: 5005, CbData: 0, LpData: 0}
	procSendMessage.Call(hwnd, 0x004A, 0, uintptr(unsafe.Pointer(&cds)))
}
```

### 4.6 AutoHotkey (v2, 64-bit & 32-bit Safe Alignment)
```ahk
#Requires AutoHotkey v2.0

SeerFindWindow() {
    return WinExist("ahk_class SeerWindowClass")
}

; Win32 struct COPYDATASTRUCT { ULONG_PTR dwData; DWORD cbData; PVOID lpData; }
; Total size is A_PtrSize * 3 (24 bytes on x64, 12 bytes on x86).
; lpData is aligned at offset A_PtrSize * 2 (16 on x64, 8 on x86).
SeerPreview(filePath) {
    hwnd := SeerFindWindow()
    if (!hwnd || filePath == "")
        return false

    if (FileExist(filePath)) {
        Loop Files filePath
            filePath := A_LoopFileFullPath
    }

    sizeInBytes := (StrLen(filePath) + 1) * 2
    pathBuf := Buffer(sizeInBytes)
    StrPut(filePath, pathBuf, "UTF-16")

    cds := Buffer(A_PtrSize * 3, 0)
    NumPut("UPtr", 5000, cds, 0)
    NumPut("UInt", sizeInBytes, cds, A_PtrSize)
    NumPut("UPtr", pathBuf.Ptr, cds, A_PtrSize * 2)

    return SendMessage(0x004A, 0, cds.Ptr, , "ahk_id " . hwnd)
}

SeerHide() {
    hwnd := SeerFindWindow()
    if (!hwnd) return 0
    cds := Buffer(A_PtrSize * 3, 0)
    NumPut("UPtr", 5005, cds, 0)
    return SendMessage(0x004A, 0, cds.Ptr, , "ahk_id " . hwnd)
}
```

---

## 5. AI Anti-Hallucination & Guardrails Checklist

When generating Seer IPC code, verify your output against these strict rules:

| Rule | Correct Behavior | Common AI Mistake (DO NOT DO) |
| :--- | :--- | :--- |
| **Transport** | Win32 `WM_COPYDATA` (0x004A) | ❌ Trying to run CLI `seer.exe -p <path>` or using sockets/named pipes |
| **Window Class** | `L"SeerWindowClass"` | ❌ Hallucinating `"SeerMain"`, `"QuickLookWindowClass"`, or `"Seer_App"` |
| **Path Format** | Always resolve to absolute canonical path (`C:\...`) | ❌ Passing relative paths like `"readme.md"` |
| **Encoding** | UTF-16LE / Unicode `wchar_t` null-terminated | ❌ Passing UTF-8 strings or raw byte arrays |
| **Interval** | Throttle calls by >= 200ms | ❌ Firing rapid calls in tight loops without throttling |
| **x64 Alignment** | `COPYDATASTRUCT` is 24 bytes, `lpData` is at offset 16 | ❌ Placing `lpData` at offset 12 (`A_PtrSize + 4`) in AHK or C FFI |

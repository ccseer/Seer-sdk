# Seer SDK IPC: AI Integration Guide

> For AI coding assistants. Use this document together with `seer/ipc.h` and `seer/ipchelper.h` as the source of truth when generating or reviewing Seer IPC code. Do not invent protocol details.
>
> This document covers out-of-process IPC. For in-process preview plugins, see [Seer-Plugins](https://github.com/ccseer/Seer-Plugins).

## AI workflow

1. Identify whether the request controls Seer or adds Spacebar support to a file manager.
2. Read `seer/ipc.h` and use its command values and `SEER_CLASS_NAME`.
3. Keep Win32 `WM_COPYDATA` as the transport.
4. Convert preview paths to absolute Windows paths; send UTF-16 with a NUL terminator and byte-accurate `cbData`.
5. Handle Seer not running, stale window handles, invalid or long paths, and the 200 ms preview throttle.
6. Provide runnable code, dependencies, target bitness, and verification steps. Do not claim a test was run unless it was actually run.

## Protocol

- Window class: `L"SeerWindowClass"`.
- Transport: `SendMessageW(hwnd, WM_COPYDATA, ..., &COPYDATASTRUCT)`, `WM_COPYDATA = 0x004A`.
- Strings are UTF-16LE; `cbData` is byte length including the trailing NUL: `(wcslen(path) + 1) * sizeof(wchar_t)`.
- `COPYDATASTRUCT` layout (64-bit): 24 bytes total (`dwData` @ 0, `cbData` @ 8, 4 bytes padding, `lpData` @ 16).
- Consecutive `SEER_INVOKE_*` calls must be at least 200 ms apart.
- Query commands may legitimately return 0; interpret results by command semantics.
- `seer::sendPathCommand` only sends data; it does not canonicalize paths or support truncation of long paths.

| Command | Value | Payload | Meaning |
|---|---:|---|---|
| `SEER_REQUEST_PATH` | 4000 | none | Seer asks a file manager for its selected path |
| `SEER_RESPONSE_PATH` | 4001 | UTF-16 path | File manager response |
| `SEER_INVOKE_W32` | 5000 | absolute path | Preview in the main window |
| `SEER_INVOKE_W32_SEP` | 5001 | absolute path | Preview in a separate window |
| `SEER_IS_VISIBLE` | 5004 | none | Main window visibility |
| `SEER_HIDE` | 5005 | none | Hide the main window |
| `SEER_GET_CURRENT_FILE` | 5006 | mapping name | Read the current preview path |
| `SEER_GET_VERSION` | 5007 | none | Version integer, e.g. 40001 = 4.0.1 |
| `SEER_IS_TRACKING_ENABLED` | 5008 | none | Tracking status |

## Minimal call pattern

```text
hwnd = FindWindowW("SeerWindowClass", null)
if hwnd == 0: report "Seer is not running"
utf16 = canonical_absolute_path(input) + NUL
cds = { dwData: 5000, cbData: utf16.byte_length, lpData: utf16.pointer }
result = SendMessageW(hwnd, WM_COPYDATA, 0, &cds)
```

Keep the UTF-16 buffer and `COPYDATASTRUCT` alive until `SendMessageW` returns. Never pass a pointer to a temporary string.

## Current-file shared memory

Create a unique named file mapping (recommended pattern: `SeerCurFile_<PID>_<Tick>`), map at least `MAX_PATH * sizeof(wchar_t)` bytes, initialize the first `wchar_t` to `L'\0'`, and send command 5006 with the mapping name as a UTF-16 payload. Read the NUL-terminated result, then always call `UnmapViewOfFile` and `CloseHandle`. Treat an empty result as unavailable.

## File-manager Spacebar integration

1. Create `%USERPROFILE%/Documents/Seer/explorers/<name>.json`:
```json
{"classname":"MyFileManagerWindowClass","windowtext":"","appname":"MyFileManager"}
```
`classname` is required.

2. Handle `WM_COPYDATA` (4000) and send the selected absolute path back to Seer:
```text
on WM_COPYDATA (cds.dwData == 4000):
    path = get_selected_absolute_path() + NUL
    seer_hwnd = FindWindowW("SeerWindowClass", null)
    if seer_hwnd != 0 and path is valid:
        resp = { dwData: 4001, cbData: path.byte_length, lpData: path.pointer }
        SendMessageW(seer_hwnd, WM_COPYDATA, own_hwnd, &resp)
```

## References and verification

- C++: `examples/cpp_ipc/main.cpp` and `seer/ipchelper.h`
- C#: `examples/csharp/SeerClient.cs`
- Python: `examples/python/seer_ipc.py` (standard-library `ctypes`)
- Rust: `examples/rust/main.rs`
- Go: `examples/go/main.go`
- PowerShell: `examples/powershell/SeerIpc.ps1`
- AutoHotkey (v2): `examples/autohotkey/seer.ahk`

Verify Seer-not-running behavior, ordinary and Unicode paths, query and hide commands, 200 ms throttling, long-path handling, and resource cleanup. For hand-written FFI, verify `COPYDATASTRUCT` field widths, pointer offsets (offset 16 on x64), calling convention, and alignment.

## Anti-hallucination checklist

- Use `WM_COPYDATA`, not a CLI, socket, or named pipe.
- Use `SeerWindowClass`; do not guess another class name.
- Use absolute UTF-16 paths and byte counts including NUL.
- On x64, `COPYDATASTRUCT` is 24 bytes and `lpData` is at offset 16 (do not put it at offset 12).
- Do not truncate long paths or treat helper functions as path canonicalizers.
- Throttle preview calls to at least 200 ms.
- Clean up shared-memory handles.
- Do not interpret every nonzero `SendMessageW` result as universal success.

## Troubleshooting

| Symptom | Check first |
|---|---|
| Window not found | Seer running, exact class name, integrity/UIPI restrictions |
| Preview does nothing | Existing absolute path, UTF-16 layout, `cbData`, throttling |
| Query returns 0 | Whether 0 is valid for that command; then verify handle and command |
| Current file is empty | Mapping name, permissions, size, initialization, cleanup order |
| x64 crash or corruption | FFI field widths, pointer offsets (offset 16 on x64), alignment, calling convention |

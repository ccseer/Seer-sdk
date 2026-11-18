# Seer SDK IPC: AI Integration Guide

> For AI coding assistants. Use this document together with `seer/ipc.h` as the source of truth when generating or reviewing Seer IPC code. Do not invent protocol details.
>
> This document covers out-of-process IPC (Named Pipe JSON-RPC 2.0, `WM_COPYDATA` `6000/6001` JSON-RPC, Dynamic Explorer `4002/4003` registration, and legacy `WM_COPYDATA` commands). For in-process preview plugins, see [Seer-Plugins](https://github.com/ccseer/Seer-Plugins).

## AI workflow

1. Identify whether the user wants:
   - **Programmatic preview control / queries / event subscriptions (Named Pipe JSON-RPC 2.0 or `6000/6001`)**: `\\.\pipe\Seer.IPC.v2.<SessionId>` or `WM_COPYDATA` `6000/6001`.
   - **File-manager Spacebar integration (Dynamic Explorer)**: `SEER_REGISTER_EXPLORER (4002)` + `SEER_UNREGISTER_EXPLORER (4003)` + `SEER_CORE_READY` + `4000/4001`.
   - **Legacy / quick hotkey `WM_COPYDATA` commands**: `5000`, `5001`, `5004`-`5010` (`examples/deprecated/` and `examples/autohotkey/seer.ahk`).
2. Read `seer/ipc.h` and use its exact constants and `SEER_CLASS_NAME`.
3. Never use `%USERPROFILE%/Documents/Seer/explorers/*.json`: Seer no longer reads explorer JSON files.
4. For preview paths, send canonical absolute Windows paths (`file://` URI accepted by JSON-RPC `seer.preview.open`). For legacy Win32 path commands, send UTF-16LE with NUL terminator and byte-accurate `cbData`.
5. Handle Seer not running, startup timing (`SEER_CORE_READY`), stale HWNDs, invalid/relative paths, `sessionId` lifetime (`-32002` across restarts), and license/active gates.
6. Provide runnable code, dependencies, target bitness, and verification steps. Do not claim a test was run unless it was actually run.

## Win32 `WM_COPYDATA` Protocol

- Window class: `L"SeerWindowClass"` (`SEER_CLASS_NAME`). Standalone viewer is `L"SeerViewerWindowClass"` (`SEER_VIEWER_CLASS_NAME`); standalone viewer returns `FALSE` (`0`) for `4002`, `4003`, and `6000` and never starts the Named Pipe server.
- Transport: `SendMessageW` / `SendMessageTimeoutW` with `WM_COPYDATA = 0x004A`.
- `COPYDATASTRUCT` layout (x64): 24 bytes total (`dwData` @ 0, `cbData` @ 8, 4 bytes padding, `lpData` @ 16).
- UTF-16LE string payloads (`4001`, `5000`, `5001`, `5006`, `5009`, `5010`): `cbData` is `(wcslen(s) + 1) * sizeof(wchar_t)`.
- `SEER_REGISTER_EXPLORER (4002)` / `SEER_UNREGISTER_EXPLORER (4003)` payloads: **fixed 8 bytes** (`ULONGLONG`, low 32 bits of sender's own `topHwnd`, zero-extended in the 8-byte struct; Seer normalizes via `GA_ROOT` and sign-extends bit 31). `wParam` **MUST** be a valid `HWND` (`IsWindow` true) owned by the calling process (typically `myTopHwnd`); passing `0` is always rejected (`FALSE`). Seer checks PID consistency between the target window and `(HWND)wParam` (`GetWindowThreadProcessId` owner) and returns `TRUE` (accepted) or `FALSE` (rejected / invalid `wParam` / not ready / viewer process / older Seer).
- Consecutive legacy `SEER_INVOKE_*` (`5000`/`5001`/`5009`/`5010`) calls should be spaced by >= 200 ms.
- Query commands (`5004`, `5007`, `5008`) may legitimately return `0`.
- Legacy `seer::sendPathCommand` (`seer/ipchelper.h`) only sends data; it does not canonicalize paths or truncate long paths.

| Command | Value | Payload | Direction / Meaning |
|---|---:|---|---|
| `SEER_REQUEST_PATH` | 4000 | none | Seer -> explorer: ask for selected path (reply synchronously within 150 ms) |
| `SEER_RESPONSE_PATH` | 4001 | UTF-16 path | Explorer -> Seer: selected absolute path response |
| `SEER_REGISTER_EXPLORER` | 4002 | `uint64` (`low32(topHwnd)`, 8 bytes) | Explorer -> Seer: register top-level window (returns `TRUE`/`FALSE`) |
| `SEER_UNREGISTER_EXPLORER` | 4003 | `uint64` (`low32(topHwnd)`, 8 bytes) | Explorer -> Seer: unregister top-level window (returns `TRUE`/`FALSE`) |
| `SEER_INVOKE_W32` | 5000 | UTF-16 absolute path | Preview in main window (legacy `WM_COPYDATA`) |
| `SEER_INVOKE_W32_SEP` | 5001 | UTF-16 absolute path | Preview in separated window (legacy `WM_COPYDATA`) |
| `SEER_IS_VISIBLE` | 5004 | none | Main window visibility (`0`/`1`) |
| `SEER_HIDE` | 5005 | none | Hide main window |
| `SEER_GET_CURRENT_FILE` | 5006 | UTF-16 mapping name | Write current preview path to shared memory (`TRUE`/`FALSE`) |
| `SEER_GET_VERSION` | 5007 | none | Integer version (`major * 10000 + minor * 100 + patch`, e.g. `40900` for `4.9.0`; dev builds return `1`) |
| `SEER_IS_TRACKING_ENABLED` | 5008 | none | Tracking status (`0`/`1`) |
| `SEER_INVOKE_W32_STANDALONE` | 5009 | UTF-16 absolute path | Preview in standalone viewer (`SeerViewer.exe`, legacy `WM_COPYDATA`) |
| `SEER_INVOKE_W32_PIN` | 5010 | UTF-16 absolute path | Preview in pinned (always-on-top) window (legacy `WM_COPYDATA`) |
| `SEER_V2_REQUEST` | 6000 | UTF-8 JSON-RPC 2.0 (no BOM, <= 64 KiB) | Client -> Seer: `wParam` MUST be caller's valid reply HWND on the sending thread |
| `SEER_V2_RESPONSE` | 6001 | UTF-8 JSON-RPC 2.0 response | Seer -> client reply HWND (`wParam` in 6000) |

## File-Manager Spacebar Integration (Dynamic Explorer)

1. On startup (after creating your top-level window `topHwnd`), find Seer (`FindWindowW(L"SeerWindowClass", nullptr)`).
2. Probe `SEER_GET_VERSION (5007)` if you need version detection (`>= 40900` for Seer `4.9.0+`; dev builds report `1`), then send `SEER_REGISTER_EXPLORER (4002)` with `SendMessageTimeoutW(..., SMTO_ABORTIFHUNG, 500, &res)`.
   - **Do NOT use `4002 == FALSE` alone to assume unsupported Seer**: `4002` also returns `FALSE` when Seer is still running `delayInit()` before registering IPC callbacks, or on protocol rejection. If Seer's version supports dynamic registration (`SEER_GET_VERSION`) and `4002` returns `FALSE`, wait for `SEER_CORE_READY` (or retry periodically against `FindWindowW(SEER_CLASS_NAME, nullptr)` if the Named Pipe failed Fail-Closed and `SEER_CORE_READY` was suppressed in Win32-degraded mode) instead of falling back to JSON files.
3. Listen for `RegisterWindowMessageW(SEER_CORE_READY_MSG)` (`L"SEER_CORE_READY"`) in your window procedure and call `RegisterToSeer(myTopHwnd)` whenever received (handles Seer starting after your app or restarting).
4. Optionally call `SEER_UNREGISTER_EXPLORER (4003)` on shutdown (Seer also prunes destroyed windows lazily).
5. When Seer sends `WM_COPYDATA` with `dwData == SEER_REQUEST_PATH (4000)`:
   - Reply **synchronously** before returning from `WM_COPYDATA` (Seer waits **150 ms** for `SEER_RESPONSE_PATH = 4001`):
   ```text
   on WM_COPYDATA (cds.dwData == 4000):
       path = get_selected_absolute_path() + NUL
       seer_hwnd = FindWindowW("SeerWindowClass", null)
       if seer_hwnd != 0 and path is valid:
           resp = { dwData: 4001, cbData: path.byte_length, lpData: path.pointer }
           SendMessageTimeoutW(seer_hwnd, WM_COPYDATA, own_hwnd, &resp, SMTO_ABORTIFHUNG, 150, null)
   ```
6. **UIPI / Elevated file manager**: If your file manager runs elevated while Seer runs non-elevated, allow messages on your top-level window:
   ```cpp
   ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
   ChangeWindowMessageFilterEx(hwnd, RegisterWindowMessageW(SEER_CORE_READY_MSG), MSGFLT_ALLOW, nullptr);
   ```

> **Note:** Older Seer versions scanned `%USERPROFILE%/Documents/Seer/explorers/<name>.json`. Current Seer ignores that directory; do **not** generate JSON-file registration.

## JSON-RPC 2.0 over Named Pipe (`\\.\pipe\Seer.IPC.v2.<SessionId>`)

- **Pipe name**: `SEER_PIPE_NAME_PREFIX + decimal(SessionId)` (`\\.\pipe\Seer.IPC.v2.<SessionId>`, from `ProcessIdToSessionId`).
- **Framing**: One single-line UTF-8 JSON-RPC 2.0 object (no BOM) terminated by `\n` (server accepts `\n` or `\r\n`, max frame **64 KiB**, no batch JSON arrays). Always call `SetNamedPipeHandleState(handle, PIPE_READMODE_MESSAGE, nullptr, nullptr)` after `CreateFileW`.
- **Mandatory client access mask & SQOS flags**:
  - Desired access: **`0x0012018B`** (`READ_CONTROL | SYNCHRONIZE | FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES`, omitting `FILE_APPEND_DATA` / `FILE_CREATE_PIPE_INSTANCE`). Do **NOT** pass `GENERIC_WRITE`: it expands to include `FILE_CREATE_PIPE_INSTANCE` (`0x4`), which the tightened server DACL denies (`ERROR_ACCESS_DENIED`).
  - Flags & attributes: **`SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`** (`0x00100000 | 0x00010000`) to prevent named-pipe impersonation attacks by a rogue squatter.
  - **Fail-closed server verification**: Immediately after opening the handle and before sending any RPC, verify:
    1. `GetNamedPipeServerSessionId(handle, &serverSessionId)` equals your own `SessionId`.
    2. Pipe object owner SID (`GetSecurityInfo(handle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, ...)`) equals your token's `TokenUser`, `WinBuiltinAdministratorsSid`, or `WinLocalSystemSid`. If not, close the handle immediately and abort.
- **Event subscriptions (`seer.events.subscribe`)**: Synchronous `ReadFile` blocks concurrent `WriteFile` on the same handle. Clients that subscribe to asynchronous events **must** open the pipe with `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION | FILE_FLAG_OVERLAPPED` and run a dedicated reader thread (see also `CancelIoEx`).
- **Connection limits & backpressure**: Up to **4 concurrent connections per PID** and **16 total**; slow readers exceeding 256 queued outbound frames / 256 KiB per connection or 4 MiB globally, or flooding >256 frames / >256 KiB unread inbound RPCs, are disconnected.

## JSON-RPC 2.0 over `WM_COPYDATA` (`6000` / `6001`)

For window-based apps wanting JSON-RPC without opening a pipe:
- Request `dwData`: `SEER_V2_REQUEST` (`6000`); payload: single UTF-8 JSON-RPC 2.0 object (no BOM, <= 64 KiB, does not need `\n`).
- Reply `dwData`: `SEER_V2_RESPONSE` (`6001`); Seer sends the UTF-8 JSON-RPC response synchronously back to **`wParam`** via `SendMessageTimeoutW(..., 500)`.
- **Strict rules for `6000`**:
  - Send directly to `FindWindowW(L"SeerWindowClass", nullptr)` — **never broadcast** (`HWND_BROADCAST`).
  - Never pass `SMTO_BLOCK` (use `SMTO_ABORTIFHUNG` with a timeout).
  - `wParam` **must** be a valid reply `HWND` owned by the same process, and `6000` **must be sent from the thread owning that `wParam` window** so `SendMessageTimeoutW(6001)` is processed synchronously during your `SendMessageTimeoutW(6000)` call (handle `6001` in its `WndProc` and do not re-send the same `6000` inside `6001` or emit side effects).
  - `seer.events.*` is **not** supported over `6000` (`-32005 TRANSPORT_UNSUPPORTED`); use Named Pipe for events.

## Methods, Events, and Error Codes

- **Methods**:
  - `seer.getInfo` (`{}`): Returns `protocolVersion` (`2`), `appVersion`, `versionInt`, `licensed`, `methods`, `events`. Always available even when unlicensed/inactive.
  - `seer.preview.open` (`{path, mode?, ifAlreadyOpen?, rect?, fullscreen?, focus?, seekMs?, viewer?}`):
    - `mode`: `"main"` (default), `"separated"`, `"pinned"`, `"standalone"`.
    - `ifAlreadyOpen` (only allowed when `mode == "main"`): `"toggle"` (default) or `"noop"`.
    - `rect` (optional object `{x, y, width, height}`, `main`/`separated`/`pinned` only; rejected in `standalone`): target physical pixel screen geometry (`width >= 1`, `height >= 1`, clamped to minimum window size `510*dpr x 430*dpr` and target screen work area). Disables content-fit auto-resize for that open. Note: on an already-showing matching `main` path, `ifAlreadyOpen: "toggle"` (default) closes the window and discards overrides; pass `ifAlreadyOpen: "noop"` to apply `rect`/other overrides.
    - `fullscreen` (optional bool): enter/exit fullscreen (supported on all modes, forwarded as `--fullscreen` in `standalone`).
    - `focus` (optional bool, `main`/`separated`/`pinned` only; rejected in `standalone`): `true` activates and focuses the preview window; `false` suppresses focus-on-open/switch regardless of `ini_preview_focus_mode`.
    - `seekMs` (optional integer `>= 0`): initial media playback position in milliseconds (supported on all modes, forwarded as `--seek-ms` in `standalone`).
    - `viewer` (optional non-empty string `<= 128` chars, `main`/`separated`/`pinned` only; rejected in `standalone`): registered enabled viewer type name. Accepts canonical built-in identities (`"Text"`, `"Image"`, `"Media"`, `"PDF"`, `"HTML"`, `"Folder"`, `"Plugin_System"`, `"Plugin_Convert"`), English display aliases (`"Web"`/`"web"` for `"HTML"`, `"System"`/`"system"` for `"Plugin_System"`, case-insensitive built-ins such as `"text"`/`"pdf"`), or a registered DLL/process plugin viewer ID. Exact registered plugin names take priority over built-in aliases; localized UI strings (e.g. `"网页"`) are not accepted. Unknown or disabled viewers return `-32602 Invalid params`.
    - `path`: absolute local path or `file://` URI (also supports special Seer paths if configured).
    - Returns `{sessionId}` (or `{closed: true}` when `"toggle"` closes an already-open matching main window; `{}` for `"standalone"`).
  - `seer.preview.close` (`{sessionId?}`): Closes the main window (default, idempotent `{}`) or a separated/pinned window (`-32003 SESSION_PINNED` if pinned).
  - `seer.preview.getState` (`{sessionId?}`): Returns `{sessionId, visible, pinned, path}` (`path` is `""` when no file is loaded).
  - `seer.preview.list` (`{}`): Returns `{sessions: [{sessionId, visible, pinned, path, isMain}]}`.
  - `seer.window.pin` (`{sessionId?}`): Pins a currently visible window (always on top); returns `-32004 INVALID_STATE` if hidden.
  - `seer.window.waggle` (`{sessionId?}`): Triggers the horizontal shake/waggle animation on a currently visible window (`{waggled: true}`); returns `-32004 INVALID_STATE` if hidden or `-32002 SESSION_NOT_FOUND` if unknown.
  - `seer.explorer.register` (`{topHwnd, name?}`), `seer.explorer.unregister` (`{topHwnd}`), `seer.explorer.suspend` (`{topHwnd, suspended}`):
    - **`topHwnd` MUST be a JSON integer `uint32` in `[1, 0xFFFFFFFF]`** (e.g. `hwnd_ptr & 0xFFFFFFFF`). Must belong to the calling PID (`-32006 REGISTRATION_REJECTED` otherwise). `name` is optional and at most 128 characters (`-32602` if exceeded). Pipe connections can take over same-PID `4002` Win32 records; distinct pipe connections cannot modify each other's registrations.
  - `seer.events.subscribe` / `seer.events.unsubscribe` (`{events: [...]}`): Pipe-only; returns `{events: [...]}` (the updated sorted list of active subscriptions for this connection).
- **Events** (Pipe notifications `{"jsonrpc":"2.0","method":"seer.event","params":{"event":...,"sessionId":..., ...}}`):
  - `preview.opened`, `preview.fileChanged`, `preview.closed`, `window.pinnedChanged`.
  - Any synchronous event triggered by an RPC (e.g. `preview.opened` or `preview.closed`) is always queued **after** that RPC's response frame.
- **Session IDs**: Opaque `<processNonce>-<seq>` strings. Invalidated on Seer restart (`-32002 SESSION_NOT_FOUND`). Do not parse or persist across restarts.
- **Error codes**:
  - Standard: `-32700` Parse error, `-32600` Invalid Request (also batch arrays), `-32601` Method not found, `-32602` Invalid params, `-32603` Internal error.
  - Seer domain: `-32001` `NOT_ACTIVE`, `-32002` `SESSION_NOT_FOUND`, `-32003` `SESSION_PINNED`, `-32004` `INVALID_STATE`, `-32005` `TRANSPORT_UNSUPPORTED`, `-32006` `REGISTRATION_REJECTED`, `-32007` `LAUNCH_FAILED`, `-32008` `OPEN_FAILED`, `-32009` `LICENSE_EXPIRED` (takes precedence over `-32001`).

## References

- C++ Dynamic Explorer: `examples/cpp_explorer/main.cpp`
- C++ Named Pipe Client: `examples/cpp_ipc/main.cpp`
- Python Named Pipe Client: `examples/python/seer_ipc.py` (standard-library `ctypes`)
- C#: `examples/csharp/SeerClient.cs`
- Rust: `examples/rust/main.rs`
- Go: `examples/go/main.go`
- PowerShell: `examples/powershell/SeerIpc.ps1`
- AutoHotkey (v2, legacy `WM_COPYDATA`): `examples/autohotkey/seer.ahk`
- Deprecated `WM_COPYDATA` (`5000`–`5010`) demos: `examples/deprecated/`

## Anti-hallucination checklist

- Do **not** generate `%USERPROFILE%/Documents/Seer/explorers/*.json`; use `4002/4003` + `SEER_CORE_READY` or `seer.explorer.register`.
- For Named Pipe, always use `\\.\pipe\Seer.IPC.v2.<SessionId>`, access `0x0012018B` (not `GENERIC_WRITE`), `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`, `PIPE_READMODE_MESSAGE`, and verify server session ID + owner SID before sending RPCs.
- For `seer.events.subscribe`, require `FILE_FLAG_OVERLAPPED` + dedicated reader thread.
- For `6000`, never broadcast, never use `SMTO_BLOCK`, and always send from the thread owning the `wParam` reply HWND.
- Always pass `topHwnd` as a JSON `uint32` in `[1, 0xFFFFFFFF]`.
- Keep existing `SEER_CLASS_NAME` (`SeerWindowClass`) and `COPYDATASTRUCT` x64 layout (24 bytes, `lpData` @ offset 16).
- Space legacy `SEER_INVOKE_*` calls by `>= 200 ms`; prefer `seer.preview.getState` (`path`) for new integrations — only if implementing legacy `SEER_GET_CURRENT_FILE (5006)`, match mapping name `^SeerCurFile_[0-9]+_[0-9]+$`, pass `cbData == buffer bytes` (min `MAX_PATH * sizeof(wchar_t)`), NUL-init first `wchar_t`, and always call `UnmapViewOfFile` + `CloseHandle`.

## Troubleshooting

| Symptom | Check first |
|---|---|
| `4002` returns `FALSE` | `wParam` is `0` or invalid HWND, Seer still starting (wait for `SEER_CORE_READY`), PID mismatch on `topHwnd`/`wParam`, hit `SeerViewerWindowClass`, or older Seer |
| Pipe `CreateFileW` access denied (5) | Used `GENERIC_WRITE` instead of `0x0012018B` |
| `REGISTRATION_REJECTED` (`-32006`) | `topHwnd` belongs to another PID or another pipe connection, or `topHwnd` invalid |
| `LICENSE_EXPIRED` (`-32009`) | Seer trial/license expired (activate license; `getInfo` and `explorer.*` still work) |
| `6001` response never arrives | Sent `6000` with `SMTO_BLOCK`, invalid `wParam`, or sent from a thread other than `wParam`'s thread |
| Pipe hangs on `WriteFile` when subscribed | Missing `FILE_FLAG_OVERLAPPED` + dedicated reader thread |
| `-32002 SESSION_NOT_FOUND` | Window was closed or Seer restarted (refresh via `seer.preview.list`) |

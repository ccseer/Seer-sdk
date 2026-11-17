# Seer SDK

Official SDK for integrating with [Seer](https://1218.io) via Windows IPC.

## Overview

Seer SDK allows third-party file managers, launcher tools, scripts, and desktop applications to trigger Seer file previews, query/control preview windows, and register file manager windows with Seer via Win32 `WM_COPYDATA` and V2 Named Pipe JSON-RPC 2.0.

Supported in any language: **C#**, **Python**, **Pure C++**, **Rust**, **Go**, **PowerShell**, **AutoHotkey**, etc.

### V2 Protocol & Breaking Change Notice

- **Dynamic File Manager Registration (Preferred)**: Starting with V2 (`4.9.0+`), third-party file managers register their top-level window dynamically via `SEER_REGISTER_EXPLORER` (`4002`) / `SEER_UNREGISTER_EXPLORER` (`4003`) or JSON-RPC `seer.explorer.register`, and re-register when Seer broadcasts `SEER_CORE_READY`.
- **Breaking Change — Legacy `explorers/*.json` Removed**: Seer V2 no longer scans `%USERPROFILE%/Documents/Seer/explorers/*.json`. `SEER_EXPLORER_FOLDER` and `SEER_JSON_KEY_*` remain in [`seer/ipc.h`](seer/ipc.h) solely for compatibility with older Seer releases (< V2).
- **Named Pipe JSON-RPC 2.0**: Connect to `\\.\pipe\Seer.IPC.v2.<SessionId>` with explicit access mask `0x0012018B` and `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`, verify server session + owner SID, and exchange newline-delimited UTF-8 JSON-RPC 2.0 frames (or send UTF-8 JSON-RPC via `WM_COPYDATA` `SEER_V2_REQUEST = 6000` / `SEER_V2_RESPONSE = 6001`).
- **Existing V1 Messages Preserved**: Existing integrations using `4000`/`4001`, `5000`/`5001`, and `5004`-`5010` continue to work unchanged.

---

## AI Assistant / LLM Context

Working with AI assistants (ChatGPT, Claude, Gemini, Cursor, Copilot)?

👉 Simply provide this URL or file to your AI: **[AI_PROMPT.md](AI_PROMPT.md)**

It provides V1 and V2 protocol specifications, message formats, JSON-RPC methods/events, security requirements, and ready-to-use templates so AI can generate accurate integration code.

---

## Examples

Ready-to-run examples are available in the [`examples/`](examples/) directory:

- **[C++ V2 Dynamic Explorer Example](examples/cpp_v2_explorer/main.cpp)**: Registers/unregisters a top-level HWND with `SEER_REGISTER_EXPLORER (4002)` / `SEER_UNREGISTER_EXPLORER (4003)`, handles `SEER_CORE_READY` restarts, and replies to `SEER_REQUEST_PATH (4000)` within 150 ms.
- **[Python V2 Named Pipe Example](examples/python/seer_ipc_v2.py)**: Pure Python stdlib (`ctypes`) Named Pipe JSON-RPC 2.0 client with `0x0012018B`, `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`, server session & owner SID verification, `seer.getInfo`, and optional `seer.explorer.register`.
- **[C# Example](examples/csharp/SeerClient.cs)**: Full P/Invoke V1 client with preview, hide, version query, and shared memory file path retrieval.
- **[Python V1 Example](examples/python/seer_ipc.py)**: Pure Python stdlib (`ctypes`) V1 `WM_COPYDATA` client without external package dependencies.
- **[Pure C++ V1 IPC Example](examples/cpp_ipc/main.cpp)**: Minimal Win32 C++ example using [`seer/ipchelper.h`](seer/ipchelper.h).
- **[Rust Example](examples/rust/main.rs)**: Lightweight Rust V1 implementation using raw Win32 FFI.
- **[Go (Golang) Example](examples/go/main.go)**: Go V1 implementation using standard library `syscall`.
- **[PowerShell Script](examples/powershell/SeerIpc.ps1)**: Native Windows automation script for triggering preview and querying status.
- **[AutoHotkey (v2) Example](examples/autohotkey/seer.ahk)**: Bind hotkeys (e.g. `Win + Space`) or automate Seer previews.

---

## Support & Links

- **Issues**: https://github.com/ccseer/Seer/issues
- **SDK Repository**: https://github.com/ccseer/Seer-sdk
- **Preview Plugins**: For developing in-process file preview renderers, see https://github.com/ccseer/Seer-Plugins

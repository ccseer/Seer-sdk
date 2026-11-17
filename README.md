# Seer SDK

Official SDK for integrating with [Seer](https://1218.io) on Windows.

## Overview

Seer SDK allows third-party file managers, launcher tools, scripts, and desktop applications to trigger Seer file previews, query/control preview windows, subscribe to lifecycle events, and dynamically register file manager windows with Seer via Named Pipe JSON-RPC 2.0 and Win32 `WM_COPYDATA`.

Supported in any language: **C++**, **Python**, **C#**, **Rust**, **Go**, **PowerShell**, **AutoHotkey**, etc.

### Protocol Highlights & Breaking Change Notice

- **Named Pipe JSON-RPC 2.0 (Primary)**: Connect to `\\.\pipe\Seer.IPC.v2.<SessionId>` with explicit access mask `0x0012018B` and `SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION`, verify server session + owner SID, and exchange newline-delimited UTF-8 JSON-RPC 2.0 frames (or send JSON-RPC over `WM_COPYDATA` `6000` / `6001`).
- **Dynamic File Manager Registration**: Third-party file managers register their top-level window dynamically via `SEER_REGISTER_EXPLORER` (`4002`) / `SEER_UNREGISTER_EXPLORER` (`4003`) or JSON-RPC `seer.explorer.register`, and re-register when Seer broadcasts `SEER_CORE_READY`.
- **Breaking Change — Legacy `explorers/*.json` Removed**: Seer no longer scans `%USERPROFILE%/Documents/Seer/explorers/*.json`, and the legacy `SEER_EXPLORER_FOLDER` / `SEER_JSON_KEY_*` macros have been removed from [`seer/ipc.h`](seer/ipc.h).
- **Backward Compatibility**: Existing integrations calling `4000`/`4001`, `5000`/`5001`, and `5004`-`5010` continue to work unchanged (legacy helper demos are archived under [`examples/deprecated/`](examples/deprecated/)).

---

## AI Assistant / LLM Context

Working with AI assistants (ChatGPT, Claude, Gemini, Cursor, Copilot)?

👉 Simply provide this URL or file to your AI: **[AI_PROMPT.md](AI_PROMPT.md)**

It provides protocol specifications, message formats, JSON-RPC methods/events, security requirements, and integration rules so AI can generate accurate Seer IPC code.

---

## Examples

Ready-to-run examples are available in the [`examples/`](examples/) directory:

- **[C++ Dynamic Explorer Example](examples/cpp_explorer/main.cpp)**: Registers/unregisters a top-level HWND with `SEER_REGISTER_EXPLORER (4002)` / `SEER_UNREGISTER_EXPLORER (4003)`, handles `SEER_CORE_READY` restarts, and replies to `SEER_REQUEST_PATH (4000)` within 150 ms.
- **[C++ Named Pipe Client](examples/cpp_ipc/main.cpp)**: Pure Win32 C++17 Named Pipe JSON-RPC 2.0 client with `0x0012018B`, SQOS Identification, and session/owner SID verification.
- **[Python Named Pipe Client](examples/python/seer_ipc.py)**: Pure Python standard library (`ctypes`) Named Pipe JSON-RPC 2.0 client without external packages.
- **[C# Example](examples/csharp/SeerClient.cs)**: Pure C# P/Invoke Named Pipe JSON-RPC 2.0 client using `System.Text.Json` (.NET Core 3.0+ / .NET 5+).
- **[Rust Example](examples/rust/main.rs)**: Pure Rust implementation using raw Win32 Named Pipe FFI.
- **[Go (Golang) Example](examples/go/main.go)**: Pure Go standard library (`syscall` + `encoding/json`) Named Pipe client.
- **[PowerShell Script](examples/powershell/SeerIpc.ps1)**: Native Windows PowerShell Named Pipe JSON-RPC automation script.
- **[AutoHotkey (v2) Example (legacy `WM_COPYDATA`)](examples/autohotkey/seer.ahk)**: Hotkey (`Win + Space`) preview automation script (`WM_COPYDATA`).
- **[Deprecated `WM_COPYDATA` Examples](examples/deprecated/)**: Legacy `5000`–`5010` `WM_COPYDATA` samples preserved for older integrations.

---

## Support & Links

- **Issues**: https://github.com/ccseer/Seer/issues
- **SDK Repository**: https://github.com/ccseer/Seer-sdk
- **Preview Plugins**: For developing in-process file preview renderers, see https://github.com/ccseer/Seer-Plugins

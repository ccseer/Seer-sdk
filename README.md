# Seer SDK

Official SDK for integrating with [Seer](https://1218.io) via Windows IPC.

## Overview

Seer SDK allows third-party file managers, launcher tools, scripts, and desktop applications to seamlessly trigger Seer file previews and control the Seer window via pure Windows API (`<Windows.h>`).

Supported in any language: **C#**, **Python**, **Pure C++**, **Rust**, **Go**, **PowerShell**, **AutoHotkey**, etc.

---

## AI Assistant / LLM Context

Working with AI assistants (ChatGPT, Claude, Gemini, Cursor, Copilot)?

👉 Simply provide this URL or file to your AI: **[AI_PROMPT.md](AI_PROMPT.md)**

It provides full protocol specifications, message formats, command enum mappings, and ready-to-use code templates across multiple languages so AI can generate accurate integration code instantly.

---

## Examples

Ready-to-run examples are available in the [`examples/`](examples/) directory:

- **[C# Example](examples/csharp/SeerClient.cs)**: Full P/Invoke client with preview, hide, version query, and shared memory file path retrieval.
- **[Python Example](examples/python/seer_ipc.py)**: Pure Python standard library (`ctypes`) implementation without any external package dependencies.
- **[Pure C++ IPC Example](examples/cpp_ipc/main.cpp)**: Minimal Win32 C++ example using [`seer/ipchelper.h`](seer/ipchelper.h).
- **[Rust Example](examples/rust/main.rs)**: Lightweight Rust implementation using raw Win32 FFI.
- **[Go (Golang) Example](examples/go/main.go)**: Go implementation using standard library `syscall`.
- **[PowerShell Script](examples/powershell/SeerIpc.ps1)**: Native Windows automation script for triggering preview and querying status.
- **[AutoHotkey (v2) Example](examples/autohotkey/seer.ahk)**: Bind hotkeys (e.g. `Win + Space`) or automate Seer previews.

---

## Support & Links

- **Issues**: https://github.com/ccseer/Seer/issues
- **SDK Repository**: https://github.com/ccseer/Seer-sdk
- **Preview Plugins**: For developing in-process file preview renderers, see https://github.com/ccseer/Seer-Plugins

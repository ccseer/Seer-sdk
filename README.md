# Seer SDK

Official SDK for integrating with [Seer](https://1218.io) and developing custom file preview plugins.

## Architecture & Capabilities

The SDK covers two distinct integration scenarios:

| Feature                | Target Audience                                                                                                          | Requirements                                                  | Supported Languages                                                 |
| :--------------------- | :----------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------ | :------------------------------------------------------------------ |
| **1. IPC Integration** | Third-party file managers, launcher tools, scripts, desktop apps triggering Seer file preview or controlling Seer window | **Pure Windows API**<br>(`<Windows.h>`)<br>*(No Qt required)* | **C#**, **Python**, **C++**, **Rust**, **Go**, **PowerShell**, etc. |
| **2. Preview Plugin**  | Developers building in-process file previewers rendered directly inside Seer                                             | **Qt 6.8**<br>**MSVC 2022**<br>**C++17**                      | **C++** (Qt Plugin DLL)                                             |

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
- **[Pure C++ IPC Example](examples/cpp_ipc/main.cpp)**: Minimal Win32 C++ example using [`seer/ipchelper.h`](seer/ipchelper.h) without Qt.
- **[Rust Example](examples/rust/main.rs)**: Lightweight Rust implementation using raw Win32 FFI.
- **[Go (Golang) Example](examples/go/main.go)**: Go implementation using standard library `syscall`.
- **[PowerShell Script](examples/powershell/SeerIpc.ps1)**: Native Windows automation script for triggering preview and querying status.
- **[AutoHotkey (v2) Example](examples/autohotkey/seer.ahk)**: Bind hotkeys (e.g. `Win + Space`) or automate Seer previews.

---

## Support & Links

- **Issues**: https://github.com/ccseer/Seer/issues
- **SDK Repository**: https://github.com/ccseer/Seer-sdk

// Seer V2 Dynamic Explorer Integration Example (Win32 / C++17)
// Demonstrates:
//   1. Registering / unregistering a top-level file manager HWND via 4002 / 4003
//   2. Re-registering automatically on SEER_CORE_READY broadcasts
//   3. Responding synchronously within 150ms to SEER_REQUEST_PATH (4000) via 4001
//
// Elevated hosts receiving messages from a non-elevated Seer must call:
//   ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
//   ChangeWindowMessageFilterEx(hwnd, g_msgSeerReady, MSGFLT_ALLOW, nullptr);

#include <windows.h>

#include <iostream>
#include <string>

#include "../../seer/ipc.h"

namespace {

UINT g_msgSeerReady = 0;

// Replace with your file manager's actual selected file or folder path (absolute).
std::wstring GetCurrentSelectedFilePath()
{
    wchar_t selfPath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
    return (len > 0 && len < MAX_PATH) ? std::wstring(selfPath, len) : std::wstring();
}

// 1. Register top-level HWND dynamically after initialization.
//    Returns false when rejected, Seer is not running, Seer is pre-V2, or Seer
//    has not yet finished delayInit(). Check SEER_GET_VERSION (5007) + wait for
//    SEER_CORE_READY if you need to distinguish startup race from pre-V2 Seer.
bool RegisterToSeer(HWND myTopHwnd)
{
    HWND seerHwnd = FindWindowW(SEER_CLASS_NAME, nullptr);
    if (!seerHwnd || !myTopHwnd) {
        return false;
    }

    // Encode only the low 32 bits in the fixed 8-byte payload (Seer sign-extends bit 31).
    ULONGLONG payload = static_cast<ULONGLONG>(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(myTopHwnd)));
    COPYDATASTRUCT cds{};
    cds.dwData = SEER_REGISTER_EXPLORER; // 4002
    cds.cbData = sizeof(payload);        // fixed 8 bytes
    cds.lpData = &payload;

    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(myTopHwnd),
                             reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 500, &result)) {
        return false;
    }
    return result != 0;
}

bool UnregisterFromSeer(HWND myTopHwnd)
{
    HWND seerHwnd = FindWindowW(SEER_CLASS_NAME, nullptr);
    if (!seerHwnd || !myTopHwnd) {
        return false;
    }

    ULONGLONG payload = static_cast<ULONGLONG>(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(myTopHwnd)));
    COPYDATASTRUCT cds{};
    cds.dwData = SEER_UNREGISTER_EXPLORER; // 4003
    cds.cbData = sizeof(payload);
    cds.lpData = &payload;

    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(myTopHwnd),
                             reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 500, &result)) {
        return false;
    }
    return result != 0;
}

LRESULT CALLBACK ExplorerWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_msgSeerReady != 0 && msg == g_msgSeerReady) {
        const bool ok = RegisterToSeer(hwnd);
        std::wcout << L"[SEER_CORE_READY] RegisterToSeer -> " << (ok ? L"TRUE" : L"FALSE") << std::endl;
        return 0;
    }

    if (msg == WM_COPYDATA) {
        const auto* pcds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (pcds && pcds->dwData == SEER_REQUEST_PATH) { // 4000
            const std::wstring selectedPath = GetCurrentSelectedFilePath();
            HWND seerHwnd = FindWindowW(SEER_CLASS_NAME, nullptr);
            if (seerHwnd && !selectedPath.empty()) {
                COPYDATASTRUCT resp{};
                resp.dwData = SEER_RESPONSE_PATH; // 4001
                resp.cbData = static_cast<DWORD>((selectedPath.length() + 1) * sizeof(wchar_t));
                resp.lpData = const_cast<wchar_t*>(selectedPath.c_str());
                // Must reply synchronously during 4000 handling; Seer waits 150 ms.
                SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(hwnd),
                                    reinterpret_cast<LPARAM>(&resp), SMTO_ABORTIFHUNG, 150, nullptr);
            }
            return TRUE;
        }
    }

    if (msg == WM_DESTROY) {
        UnregisterFromSeer(hwnd);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    const bool onceMode = (argc > 1 && std::wstring(argv[1]) == L"--once");

    g_msgSeerReady = RegisterWindowMessageW(SEER_CORE_READY_MSG);

    const HINSTANCE hInst = GetModuleHandleW(nullptr);
    constexpr wchar_t kClassName[] = L"SeerSdkV2ExplorerSampleWnd";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ExplorerWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) {
        std::wcerr << L"RegisterClassExW failed: " << GetLastError() << std::endl;
        return 1;
    }

    HWND hwnd = CreateWindowExW(0, kClassName, L"Seer SDK V2 Explorer Sample",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 320, 200,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        std::wcerr << L"CreateWindowExW failed: " << GetLastError() << std::endl;
        return 1;
    }

    // If your process runs elevated, allow messages from non-elevated Seer:
    // ChangeWindowMessageFilterEx(hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);
    // if (g_msgSeerReady) ChangeWindowMessageFilterEx(hwnd, g_msgSeerReady, MSGFLT_ALLOW, nullptr);

    const bool registered = RegisterToSeer(hwnd);
    std::wcout << L"Initial RegisterToSeer(" << reinterpret_cast<ULONG_PTR>(hwnd) << L") -> "
               << (registered ? L"TRUE" : L"FALSE (Seer not running or still starting)") << std::endl;

    if (onceMode) {
        DestroyWindow(hwnd);
        return 0;
    }

    std::wcout << L"Listening for SEER_CORE_READY and SEER_REQUEST_PATH (4000). Close window or Ctrl+C to exit."
               << std::endl;

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

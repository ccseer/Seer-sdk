#pragma once

#include <Windows.h>

#include <string>

#include "ipc.h"

namespace seer_ipc {
inline HWND findSeerWindow()
{
    return FindWindowW(SEER_CLASS_NAME, nullptr);
}

// Send a simple command (no payload) to Seer
inline LRESULT sendSimpleCommand(HWND seer_hwnd, int command)
{
    if (!seer_hwnd) {
        return false;
    }

    COPYDATASTRUCT cds;
    cds.dwData = command;
    cds.cbData = 0;
    cds.lpData = nullptr;
    return SendMessageW(seer_hwnd, WM_COPYDATA, 0, (LPARAM)&cds);
}

// Send a command with a string payload (e.g., file path) to Seer
inline bool sendPathCommand(HWND seer_hwnd,
                            int command,
                            const std::wstring &path)
{
    if (!seer_hwnd || path.empty()) {
        return false;
    }

    COPYDATASTRUCT cds;
    cds.dwData = command;
    cds.cbData = static_cast<DWORD>((path.length() + 1) * sizeof(wchar_t));
    cds.lpData = (LPVOID)path.c_str();
    return SendMessageW(seer_hwnd, WM_COPYDATA, 0, (LPARAM)&cds) != 0;
}

// Request the currently previewed file (buffer provided by caller)
inline bool requestCurrentFile(HWND seer_hwnd, std::wstring &out_path)
{
    out_path.clear();
    if (!seer_hwnd) {
        return false;
    }

    wchar_t map_name[MAX_PATH] = {0};
    swprintf_s(map_name, L"SeerCurFile_%u_%llu", GetCurrentProcessId(),
               GetTickCount64());
    constexpr size_t buf_bytes = MAX_PATH * sizeof(wchar_t);
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                    PAGE_READWRITE, 0, buf_bytes, map_name);
    if (!map) {
        return false;
    }
    auto view = static_cast<wchar_t *>(
        MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, buf_bytes));
    if (!view) {
        CloseHandle(map);
        return false;
    }
    view[0] = L'\0';

    COPYDATASTRUCT cds;
    cds.dwData = SEER_GET_CURRENT_FILE;
    cds.cbData = buf_bytes;
    cds.lpData = map_name;
    SendMessageW(seer_hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds));
    if (view[0] != L'\0') {
        out_path.assign(view);
    }
    UnmapViewOfFile(view);
    CloseHandle(map);
    return !out_path.empty();
}

// Request a preview of a file (invoke preview window)
inline bool invokePreview(HWND seer_hwnd, const std::wstring &path)
{
    return sendPathCommand(seer_hwnd, SEER_INVOKE_W32, path);
}

// Request a preview in a separated window
inline bool invokePreviewSeparated(HWND seer_hwnd, const std::wstring &path)
{
    return sendPathCommand(seer_hwnd, SEER_INVOKE_W32_SEP, path);
}

// Query if tracking is enabled
inline bool requestTrackingStatus(HWND seer_hwnd)
{
    return sendSimpleCommand(seer_hwnd, SEER_IS_TRACKING_ENABLED);
}

// Check if the Seer main window is visible
inline bool isMainWindowVisible(HWND seer_hwnd)
{
    return sendSimpleCommand(seer_hwnd, SEER_IS_VISIBLE);
}

// Request Seer version
inline int requestVersion(HWND seer_hwnd)
{
    return sendSimpleCommand(seer_hwnd, SEER_GET_VERSION);
}

// Hide the Seer main window
inline void hideMainWindow(HWND seer_hwnd)
{
    sendSimpleCommand(seer_hwnd, SEER_HIDE);
}

}  // namespace seer_ipc

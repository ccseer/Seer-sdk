#include <iostream>
#include <string>

// seer/ipchelper.h only depends on <Windows.h> and <string>
// No Qt dependency required!
#include "../../seer/ipchelper.h"

int wmain(int argc, wchar_t* argv[])
{
    std::wcout << L"=== Seer Pure C++ IPC Demo (No Qt Required) ===" << std::endl;

    HWND seer_hwnd = seer::findSeerWindow();
    if (!seer_hwnd) {
        std::wcout << L"Seer is not running! Please start Seer first." << std::endl;
        return 1;
    }

    int version = seer::requestVersion(seer_hwnd);
    std::wcout << L"Seer Version: " << version << std::endl;
    std::wcout << L"Is Visible: " << (seer::isMainWindowVisible(seer_hwnd) ? L"true" : L"false") << std::endl;
    std::wcout << L"Is Tracking Enabled: " << (seer::requestTrackingStatus(seer_hwnd) ? L"true" : L"false") << std::endl;

    std::wstring current_file;
    if (seer::requestCurrentFile(seer_hwnd, current_file)) {
        std::wcout << L"Current previewed file: " << current_file << std::endl;
    }

    if (argc > 1) {
        std::wstring target_path = argv[1];
        wchar_t full_path[MAX_PATH] = {0};
        if (GetFullPathNameW(target_path.c_str(), MAX_PATH, full_path, nullptr)) {
            target_path = full_path;
        }

        std::wcout << L"Invoking preview for: " << target_path << std::endl;
        seer::invokePreview(seer_hwnd, target_path);
    } else {
        std::wcout << L"Usage: SeerCppDemo.exe <file_path_to_preview>" << std::endl;
    }

    return 0;
}

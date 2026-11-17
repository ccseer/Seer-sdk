// Seer Pure C++17 Named Pipe JSON-RPC 2.0 Client Example (Win32 SDK only)
// Requires only <Windows.h> / <sddl.h> / User32+Advapi32 (no external libraries).
//
// Security contract implemented:
//   - Connects to \\.\pipe\Seer.IPC.v2.<SessionId>
//   - Desired access: 0x0012018B (READ_CONTROL | SYNCHRONIZE | FILE_READ_DATA |
//     FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES)
//     (never GENERIC_WRITE, which implies FILE_CREATE_PIPE_INSTANCE / 0x4)
//   - SQOS flags: SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION
//   - Verifies server session ID + owner SID before sending RPCs
//   - Switches pipe handle to PIPE_READMODE_MESSAGE

#include <aclapi.h>
#include <windows.h>

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../../seer/ipc.h"

namespace {

constexpr DWORD kMaxFrameBytes = 64 * 1024;
constexpr DWORD kClientDesiredAccess =
    READ_CONTROL | SYNCHRONIZE | FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

struct HandleGuard {
    HANDLE h = INVALID_HANDLE_VALUE;
    ~HandleGuard()
    {
        if (h != INVALID_HANDLE_VALUE && h != nullptr) {
            CloseHandle(h);
        }
    }
};

struct LocalMemGuard {
    HLOCAL p = nullptr;
    ~LocalMemGuard()
    {
        if (p) {
            LocalFree(p);
        }
    }
};

std::string wideToUtf8(const std::wstring& s)
{
    if (s.empty()) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

std::string escapeJson(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (unsigned char ch : input) {
        switch (ch) {
        case '\"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                char buf[8]{};
                std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(ch));
                out += buf;
            } else {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

bool queryCurrentUserSid(std::vector<BYTE>& outBuf, PSID& outSid)
{
    HANDLE rawToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) {
        return false;
    }
    HandleGuard tokGuard{rawToken};

    DWORD needed = 0;
    GetTokenInformation(rawToken, TokenUser, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0) {
        return false;
    }
    outBuf.resize(needed);
    if (!GetTokenInformation(rawToken, TokenUser, outBuf.data(), needed, &needed)) {
        return false;
    }
    outSid = reinterpret_cast<TOKEN_USER*>(outBuf.data())->User.Sid;
    return true;
}

bool createWellKnownSidBuf(WELL_KNOWN_SID_TYPE type, std::vector<BYTE>& outBuf)
{
    DWORD size = SECURITY_MAX_SID_SIZE;
    outBuf.resize(size);
    if (!CreateWellKnownSid(type, nullptr, outBuf.data(), &size)) {
        return false;
    }
    outBuf.resize(size);
    return true;
}

bool verifyPipeServer(HANDLE pipeHandle, DWORD expectedSessionId)
{
    ULONG serverSessionId = 0;
    if (!GetNamedPipeServerSessionId(pipeHandle, &serverSessionId)) {
        std::wcerr << L"GetNamedPipeServerSessionId failed: " << GetLastError() << std::endl;
        return false;
    }
    if (serverSessionId != expectedSessionId) {
        std::wcerr << L"Pipe server session mismatch: got " << serverSessionId << L", expected " << expectedSessionId << std::endl;
        return false;
    }

    PSID ownerSid = nullptr;
    PSECURITY_DESCRIPTOR pSd = nullptr;
    const DWORD secErr = GetSecurityInfo(pipeHandle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &ownerSid, nullptr, nullptr, nullptr, &pSd);
    if (secErr != ERROR_SUCCESS) {
        std::wcerr << L"GetSecurityInfo failed: " << secErr << std::endl;
        return false;
    }
    LocalMemGuard sdGuard{pSd};

    std::vector<BYTE> userBuf;
    PSID userSid = nullptr;
    std::vector<BYTE> adminSid;
    std::vector<BYTE> systemSid;
    if (!queryCurrentUserSid(userBuf, userSid) ||
        !createWellKnownSidBuf(WinBuiltinAdministratorsSid, adminSid) ||
        !createWellKnownSidBuf(WinLocalSystemSid, systemSid)) {
        std::wcerr << L"Failed to query local SIDs: " << GetLastError() << std::endl;
        return false;
    }

    if (!EqualSid(ownerSid, userSid) && !EqualSid(ownerSid, adminSid.data()) && !EqualSid(ownerSid, systemSid.data())) {
        std::wcerr << L"Untrusted pipe server owner SID" << std::endl;
        return false;
    }
    return true;
}

bool sendFrame(HANDLE hPipe, const std::string& jsonLine)
{
    const std::string framed = jsonLine + "\n";
    DWORD written = 0;
    if (!WriteFile(hPipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr)) {
        std::wcerr << L"WriteFile failed: " << GetLastError() << std::endl;
        return false;
    }
    return written == framed.size();
}

bool recvFrame(HANDLE hPipe, std::string& outLine)
{
    outLine.clear();
    std::vector<char> buf(kMaxFrameBytes);
    for (;;) {
        DWORD readBytes = 0;
        const BOOL ok = ReadFile(hPipe, buf.data(), kMaxFrameBytes, &readBytes, nullptr);
        if (readBytes > 0) {
            outLine.append(buf.data(), readBytes);
        }
        if (ok) {
            break;
        }
        const DWORD err = GetLastError();
        if (err == ERROR_MORE_DATA) {
            if (outLine.size() > kMaxFrameBytes) {
                std::wcerr << L"Frame exceeded 64 KiB limit" << std::endl;
                return false;
            }
            continue;
        }
        std::wcerr << L"ReadFile failed: " << err << std::endl;
        return false;
    }
    while (!outLine.empty() && (outLine.back() == '\n' || outLine.back() == '\r')) {
        outLine.pop_back();
    }
    return true;
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        std::wcerr << L"ProcessIdToSessionId failed: " << GetLastError() << std::endl;
        return 1;
    }

    const std::wstring pipeName = std::wstring(SEER_PIPE_NAME_PREFIX) + std::to_wstring(sessionId);
    HANDLE rawPipe = CreateFileW(pipeName.c_str(), kClientDesiredAccess, 0, nullptr, OPEN_EXISTING,
                                 SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr);
    if (rawPipe == INVALID_HANDLE_VALUE) {
        std::wcerr << L"CreateFileW(" << pipeName << L") failed: " << GetLastError() << L" (is Seer running?)" << std::endl;
        return 1;
    }
    HandleGuard pipeGuard{rawPipe};

    if (!verifyPipeServer(rawPipe, sessionId)) {
        return 1;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(rawPipe, &mode, nullptr, nullptr)) {
        std::wcerr << L"SetNamedPipeHandleState failed: " << GetLastError() << std::endl;
        return 1;
    }

    std::string reply;
    if (!sendFrame(rawPipe, R"({"jsonrpc":"2.0","id":1,"method":"seer.getInfo","params":{}})") || !recvFrame(rawPipe, reply)) {
        return 1;
    }
    std::cout << "seer.getInfo: " << reply << std::endl;

    if (!sendFrame(rawPipe, R"({"jsonrpc":"2.0","id":2,"method":"seer.preview.list","params":{}})") || !recvFrame(rawPipe, reply)) {
        return 1;
    }
    std::cout << "seer.preview.list: " << reply << std::endl;

    if (argc > 1) {
        wchar_t fullPath[MAX_PATH] = {};
        std::wstring target = argv[1];
        if (GetFullPathNameW(target.c_str(), MAX_PATH, fullPath, nullptr)) {
            target = fullPath;
        }
        const std::string escapedPath = escapeJson(wideToUtf8(target));
        const std::string openReq = std::string(R"({"jsonrpc":"2.0","id":3,"method":"seer.preview.open","params":{"path":")") +
                                    escapedPath + R"(","mode":"main","ifAlreadyOpen":"noop"}})";
        if (!sendFrame(rawPipe, openReq) || !recvFrame(rawPipe, reply)) {
            return 1;
        }
        std::cout << "seer.preview.open: " << reply << std::endl;
    } else {
        std::cout << "Usage: SeerCppDemo.exe <file_path_to_preview>" << std::endl;
    }
    return 0;
}

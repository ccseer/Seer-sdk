#pragma once

// Seer IPC v2 C++17 Header-Only Helper (Win32 SDK only; no Qt or JSON library required).
// Covers:
//   1. Hardened Named Pipe JSON-RPC 2.0 client (\\.\pipe\Seer.IPC.v2.<SessionId>)
//      with mandatory 0x0012018B access mask, SECURITY_IDENTIFICATION SQOS,
//      server session ID + owner SID verification, and PIPE_READMODE_MESSAGE.
//   2. Dynamic Explorer registration & 4001 path reply (4002 / 4003 / 4001).
//   3. Synchronous WM_COPYDATA JSON-RPC 2.0 transport (SEER_V2_REQUEST = 6000).
//
// Link dependencies: User32.lib, Advapi32.lib (auto-linked on MSVC below).

#include <Windows.h>
#include <aclapi.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ipc.h"

#ifdef _MSC_VER
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")
#endif

namespace seer::v2 {

inline constexpr DWORD kMaxFrameBytes = 64 * 1024;
// Omit GENERIC_WRITE (which includes FILE_CREATE_PIPE_INSTANCE / 0x4).
inline constexpr DWORD kClientDesiredAccess =
    READ_CONTROL | SYNCHRONIZE | FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;

namespace detail {

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

inline bool queryCurrentUserSid(std::vector<BYTE>& outBuf, PSID& outSid)
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

inline bool createWellKnownSidBuf(WELL_KNOWN_SID_TYPE type, std::vector<BYTE>& outBuf)
{
    DWORD size = SECURITY_MAX_SID_SIZE;
    outBuf.resize(size);
    if (!CreateWellKnownSid(type, nullptr, outBuf.data(), &size)) {
        return false;
    }
    outBuf.resize(size);
    return true;
}

inline bool verifyPipeServer(HANDLE pipeHandle, DWORD expectedSessionId, DWORD* outErr)
{
    ULONG serverSessionId = 0;
    if (!GetNamedPipeServerSessionId(pipeHandle, &serverSessionId)) {
        if (outErr) {
            *outErr = GetLastError();
        }
        return false;
    }
    if (serverSessionId != expectedSessionId) {
        if (outErr) {
            *outErr = ERROR_ACCESS_DENIED;
        }
        return false;
    }

    PSID ownerSid = nullptr;
    PSECURITY_DESCRIPTOR pSd = nullptr;
    const DWORD secErr =
        GetSecurityInfo(pipeHandle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION, &ownerSid, nullptr, nullptr, nullptr, &pSd);
    if (secErr != ERROR_SUCCESS) {
        if (outErr) {
            *outErr = secErr;
        }
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
        if (outErr) {
            *outErr = GetLastError();
        }
        return false;
    }

    if (!EqualSid(ownerSid, userSid) && !EqualSid(ownerSid, adminSid.data()) && !EqualSid(ownerSid, systemSid.data())) {
        if (outErr) {
            *outErr = ERROR_ACCESS_DENIED;
        }
        return false;
    }
    return true;
}

// Extract top-level JSON integer "id" without confusing nested "id" / "sessionId" fields.
inline bool extractTopLevelUintId(const std::string& json, std::uint64_t& outId)
{
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    const size_t n = json.size();
    for (size_t i = 0; i < n; ++i) {
        const char ch = json[i];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            if (depth == 1 && i + 4 <= n && json.compare(i, 4, "\"id\"") == 0) {
                size_t j = i + 4;
                while (j < n && (json[j] == ' ' || json[j] == '\t' || json[j] == '\r' || json[j] == '\n')) {
                    ++j;
                }
                if (j < n && json[j] == ':') {
                    ++j;
                    while (j < n && (json[j] == ' ' || json[j] == '\t' || json[j] == '\r' || json[j] == '\n')) {
                        ++j;
                    }
                    if (j < n && json[j] >= '0' && json[j] <= '9') {
                        std::uint64_t parsed = 0;
                        while (j < n && json[j] >= '0' && json[j] <= '9') {
                            parsed = parsed * 10 + static_cast<std::uint64_t>(json[j] - '0');
                            ++j;
                        }
                        outId = parsed;
                        return true;
                    }
                    return false;
                }
            }
            inString = true;
            continue;
        }
        if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            --depth;
        }
    }
    return false;
}

inline std::string appendOptionalSessionId(const std::string& sessionId)
{
    return sessionId.empty() ? std::string() : (std::string(R"("sessionId":")") + sessionId + "\"");
}

}  // namespace detail

inline std::string wideToUtf8(const std::wstring& s)
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

inline std::wstring utf8ToWide(const std::string& s)
{
    if (s.empty()) {
        return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
    return out;
}

inline std::string escapeJson(const std::string& input)
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

inline std::string escapeJsonWide(const std::wstring& input)
{
    return escapeJson(wideToUtf8(input));
}

inline std::wstring pipeNameForCurrentSession(DWORD* outSessionId = nullptr)
{
    DWORD sessionId = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId)) {
        return {};
    }
    if (outSessionId) {
        *outSessionId = sessionId;
    }
    return std::wstring(SEER_PIPE_NAME_PREFIX) + std::to_wstring(sessionId);
}

inline std::string buildRequest(std::uint64_t id, const std::string& method, const std::string& paramsJson = "{}")
{
    return std::string(R"({"jsonrpc":"2.0","id":)") + std::to_string(id) + R"(,"method":")" +
           escapeJson(method) + R"(","params":)" + (paramsJson.empty() ? std::string("{}") : paramsJson) + "}";
}

inline HWND findSeerWindow()
{
    return FindWindowW(SEER_CLASS_NAME, nullptr);
}

inline bool registerExplorer(HWND seerHwnd, HWND myTopHwnd, DWORD timeoutMs = 500)
{
    if (!seerHwnd || !myTopHwnd) {
        return false;
    }
    const ULONGLONG payload = static_cast<ULONGLONG>(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(myTopHwnd)));
    COPYDATASTRUCT cds{};
    cds.dwData = SEER_REGISTER_EXPLORER;
    cds.cbData = sizeof(payload);
    cds.lpData = const_cast<ULONGLONG*>(&payload);
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(myTopHwnd), reinterpret_cast<LPARAM>(&cds),
                             SMTO_ABORTIFHUNG, timeoutMs, &result)) {
        return false;
    }
    return result != 0;
}

inline bool unregisterExplorer(HWND seerHwnd, HWND myTopHwnd, DWORD timeoutMs = 500)
{
    if (!seerHwnd || !myTopHwnd) {
        return false;
    }
    const ULONGLONG payload = static_cast<ULONGLONG>(static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(myTopHwnd)));
    COPYDATASTRUCT cds{};
    cds.dwData = SEER_UNREGISTER_EXPLORER;
    cds.cbData = sizeof(payload);
    cds.lpData = const_cast<ULONGLONG*>(&payload);
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(myTopHwnd), reinterpret_cast<LPARAM>(&cds),
                             SMTO_ABORTIFHUNG, timeoutMs, &result)) {
        return false;
    }
    return result != 0;
}

inline bool replyExplorerPath(HWND seerHwnd, HWND myTopHwnd, const std::wstring& absPath, DWORD timeoutMs = 150)
{
    if (!seerHwnd || !myTopHwnd || absPath.empty()) {
        return false;
    }
    COPYDATASTRUCT resp{};
    resp.dwData = SEER_RESPONSE_PATH;
    resp.cbData = static_cast<DWORD>((absPath.length() + 1) * sizeof(wchar_t));
    resp.lpData = const_cast<wchar_t*>(absPath.c_str());
    return SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(myTopHwnd), reinterpret_cast<LPARAM>(&resp),
                               SMTO_ABORTIFHUNG, timeoutMs, nullptr) != 0;
}

inline bool sendV2JsonRpc(HWND seerHwnd,
                          HWND replyHwnd,
                          const std::string& jsonUtf8,
                          DWORD timeoutMs = 500,
                          DWORD_PTR* outResult = nullptr)
{
    if (!seerHwnd || !replyHwnd || jsonUtf8.empty() || jsonUtf8.size() > kMaxFrameBytes) {
        return false;
    }
    COPYDATASTRUCT cds{};
    cds.dwData = SEER_V2_REQUEST;
    cds.cbData = static_cast<DWORD>(jsonUtf8.size());
    cds.lpData = const_cast<char*>(jsonUtf8.data());
    DWORD_PTR res = 0;
    if (!SendMessageTimeoutW(seerHwnd, WM_COPYDATA, reinterpret_cast<WPARAM>(replyHwnd), reinterpret_cast<LPARAM>(&cds),
                             SMTO_ABORTIFHUNG, timeoutMs, &res)) {
        return false;
    }
    if (outResult) {
        *outResult = res;
    }
    return res != 0;
}

struct OpenOptions {
    std::string mode = "main";
    std::string ifAlreadyOpen;
    std::optional<RECT> rect;  // left, top, right, bottom -> serialized as x, y, width, height
    std::optional<bool> fullscreen;
    std::optional<bool> focus;
    std::optional<std::uint64_t> seekMs;
    std::string viewer;
};

class PipeClient {
public:
    using EventCallback = std::function<void(const std::string&)>;

    PipeClient() = default;
    ~PipeClient() { disconnect(); }

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    PipeClient(PipeClient&& other) noexcept
        : m_pipe(other.m_pipe),
          m_overlapped(other.m_overlapped),
          m_nextId(other.m_nextId),
          m_onEvent(std::move(other.m_onEvent))
    {
        other.m_pipe = INVALID_HANDLE_VALUE;
        other.m_overlapped = false;
    }

    PipeClient& operator=(PipeClient&& other) noexcept
    {
        if (this != &other) {
            disconnect();
            m_pipe = other.m_pipe;
            m_overlapped = other.m_overlapped;
            m_nextId = other.m_nextId;
            m_onEvent = std::move(other.m_onEvent);
            other.m_pipe = INVALID_HANDLE_VALUE;
            other.m_overlapped = false;
        }
        return *this;
    }

    void setEventCallback(EventCallback cb) { m_onEvent = std::move(cb); }

    bool connect(bool overlapped = false, DWORD* outWin32Error = nullptr)
    {
        disconnect();

        DWORD sessionId = 0;
        const std::wstring pipeName = pipeNameForCurrentSession(&sessionId);
        if (pipeName.empty()) {
            if (outWin32Error) {
                *outWin32Error = GetLastError();
            }
            return false;
        }

        const DWORD flags =
            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION | (overlapped ? FILE_FLAG_OVERLAPPED : 0);
        HANDLE rawPipe =
            CreateFileW(pipeName.c_str(), kClientDesiredAccess, 0, nullptr, OPEN_EXISTING, flags, nullptr);
        if (rawPipe == INVALID_HANDLE_VALUE) {
            if (outWin32Error) {
                *outWin32Error = GetLastError();
            }
            return false;
        }

        if (!detail::verifyPipeServer(rawPipe, sessionId, outWin32Error)) {
            CloseHandle(rawPipe);
            return false;
        }

        DWORD mode = PIPE_READMODE_MESSAGE;
        if (!SetNamedPipeHandleState(rawPipe, &mode, nullptr, nullptr)) {
            if (outWin32Error) {
                *outWin32Error = GetLastError();
            }
            CloseHandle(rawPipe);
            return false;
        }

        m_pipe = rawPipe;
        m_overlapped = overlapped;
        return true;
    }

    void disconnect()
    {
        if (m_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(m_pipe);
            m_pipe = INVALID_HANDLE_VALUE;
        }
        m_overlapped = false;
    }

    bool isConnected() const noexcept { return m_pipe != INVALID_HANDLE_VALUE; }
    HANDLE handle() const noexcept { return m_pipe; }

    bool sendFrame(const std::string& jsonLine, DWORD* outWin32Error = nullptr)
    {
        if (m_pipe == INVALID_HANDLE_VALUE || jsonLine.empty() || jsonLine.size() > kMaxFrameBytes) {
            if (outWin32Error) {
                *outWin32Error = ERROR_INVALID_PARAMETER;
            }
            return false;
        }
        const std::string framed = jsonLine + "\n";
        if (framed.size() > kMaxFrameBytes) {
            if (outWin32Error) {
                *outWin32Error = ERROR_INVALID_PARAMETER;
            }
            return false;
        }

        if (!m_overlapped) {
            DWORD written = 0;
            if (!WriteFile(m_pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, nullptr)) {
                if (outWin32Error) {
                    *outWin32Error = GetLastError();
                }
                return false;
            }
            return written == framed.size();
        }

        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            if (outWin32Error) {
                *outWin32Error = GetLastError();
            }
            return false;
        }
        detail::HandleGuard evGuard{ov.hEvent};

        DWORD written = 0;
        const BOOL ok = WriteFile(m_pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                if (outWin32Error) {
                    *outWin32Error = err;
                }
                return false;
            }
            if (!GetOverlappedResult(m_pipe, &ov, &written, TRUE)) {
                if (outWin32Error) {
                    *outWin32Error = GetLastError();
                }
                return false;
            }
        }
        return written == framed.size();
    }

    bool recvFrame(std::string& outLine, DWORD* outWin32Error = nullptr)
    {
        outLine.clear();
        if (m_pipe == INVALID_HANDLE_VALUE) {
            if (outWin32Error) {
                *outWin32Error = ERROR_INVALID_HANDLE;
            }
            return false;
        }

        std::vector<char> buf(kMaxFrameBytes);
        for (;;) {
            DWORD readBytes = 0;
            BOOL ok = FALSE;
            DWORD err = ERROR_SUCCESS;

            if (!m_overlapped) {
                ok = ReadFile(m_pipe, buf.data(), kMaxFrameBytes, &readBytes, nullptr);
                if (!ok) {
                    err = GetLastError();
                }
            } else {
                OVERLAPPED ov{};
                ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!ov.hEvent) {
                    if (outWin32Error) {
                        *outWin32Error = GetLastError();
                    }
                    return false;
                }
                detail::HandleGuard evGuard{ov.hEvent};
                ok = ReadFile(m_pipe, buf.data(), kMaxFrameBytes, &readBytes, &ov);
                if (!ok) {
                    err = GetLastError();
                    if (err == ERROR_IO_PENDING) {
                        ok = GetOverlappedResult(m_pipe, &ov, &readBytes, TRUE);
                        err = ok ? ERROR_SUCCESS : GetLastError();
                    }
                }
            }

            if (readBytes > 0) {
                outLine.append(buf.data(), readBytes);
            }
            if (ok) {
                break;
            }
            if (err == ERROR_MORE_DATA) {
                if (outLine.size() > kMaxFrameBytes) {
                    if (outWin32Error) {
                        *outWin32Error = ERROR_INSUFFICIENT_BUFFER;
                    }
                    return false;
                }
                continue;
            }
            if (outWin32Error) {
                *outWin32Error = err;
            }
            return false;
        }

        while (!outLine.empty() && (outLine.back() == '\n' || outLine.back() == '\r')) {
            outLine.pop_back();
        }
        return true;
    }

    bool call(const std::string& method,
              const std::string& paramsJson,
              std::string& outResponseJson,
              std::uint64_t* outId = nullptr,
              DWORD* outWin32Error = nullptr)
    {
        const std::uint64_t id = m_nextId++;
        if (outId) {
            *outId = id;
        }
        if (!sendFrame(buildRequest(id, method, paramsJson), outWin32Error)) {
            return false;
        }
        for (;;) {
            std::string frame;
            if (!recvFrame(frame, outWin32Error)) {
                return false;
            }
            std::uint64_t frameId = 0;
            if (detail::extractTopLevelUintId(frame, frameId)) {
                if (frameId == id) {
                    outResponseJson = std::move(frame);
                    return true;
                }
                continue;
            }
            if (m_onEvent) {
                m_onEvent(frame);
            }
        }
    }

    bool callGetInfo(std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.getInfo", "{}", outResp, nullptr, outErr);
    }

    bool callAppGetState(std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.app.getState", "{}", outResp, nullptr, outErr);
    }

    bool callViewersList(bool includeDisabled, std::string& outResp, DWORD* outErr = nullptr)
    {
        const std::string params = includeDisabled ? R"({"includeDisabled":true})" : R"({})";
        return call("seer.viewers.list", params, outResp, nullptr, outErr);
    }

    bool callPreviewOpen(const std::wstring& path,
                         const OpenOptions& opts,
                         std::string& outResp,
                         DWORD* outErr = nullptr)
    {
        std::string params = std::string(R"({"path":")") + escapeJsonWide(path) + "\"";
        if (!opts.mode.empty()) {
            params += R"(,"mode":")" + escapeJson(opts.mode) + "\"";
        }
        if (!opts.ifAlreadyOpen.empty()) {
            params += R"(,"ifAlreadyOpen":")" + escapeJson(opts.ifAlreadyOpen) + "\"";
        }
        if (opts.rect.has_value()) {
            const LONG w = opts.rect->right - opts.rect->left;
            const LONG h = opts.rect->bottom - opts.rect->top;
            params += R"(,"rect":{"x":)" + std::to_string(opts.rect->left) + R"(,"y":)" +
                      std::to_string(opts.rect->top) + R"(,"width":)" + std::to_string(w) + R"(,"height":)" +
                      std::to_string(h) + "}";
        }
        if (opts.fullscreen.has_value()) {
            params += std::string(R"(,"fullscreen":)") + (*opts.fullscreen ? "true" : "false");
        }
        if (opts.focus.has_value()) {
            params += std::string(R"(,"focus":)") + (*opts.focus ? "true" : "false");
        }
        if (opts.seekMs.has_value()) {
            params += R"(,"seekMs":)" + std::to_string(*opts.seekMs);
        }
        if (!opts.viewer.empty()) {
            params += R"(,"viewer":")" + escapeJson(opts.viewer) + "\"";
        }
        params += "}";
        return call("seer.preview.open", params, outResp, nullptr, outErr);
    }

    bool callPreviewClose(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.close", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callPreviewGetState(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.getState", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callPreviewList(std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.list", "{}", outResp, nullptr, outErr);
    }

    bool callSwitchViewer(const std::string& sessionId,
                          const std::string& viewer,
                          std::string& outResp,
                          DWORD* outErr = nullptr)
    {
        std::string inner = R"("viewer":")" + escapeJson(viewer) + "\"";
        if (!sessionId.empty()) {
            inner = detail::appendOptionalSessionId(escapeJson(sessionId)) + "," + inner;
        }
        return call("seer.preview.switchViewer", "{" + inner + "}", outResp, nullptr, outErr);
    }

    bool callReload(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.reload", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callSeparate(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.separate", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callCloseAllSub(std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.closeAllSub", "{}", outResp, nullptr, outErr);
    }

    bool callReveal(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.reveal", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callOpenDefault(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.openDefault", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}",
                    outResp, nullptr, outErr);
    }

    bool callGetProperties(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.getProperties", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}",
                    outResp, nullptr, outErr);
    }

    bool callListControls(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.preview.listControls", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}",
                    outResp, nullptr, outErr);
    }

    bool callTriggerControl(const std::string& sessionId,
                            const std::string& controlId,
                            std::string& outResp,
                            DWORD* outErr = nullptr)
    {
        std::string inner = R"("id":")" + escapeJson(controlId) + "\"";
        if (!sessionId.empty()) {
            inner = detail::appendOptionalSessionId(escapeJson(sessionId)) + "," + inner;
        }
        return call("seer.preview.triggerControl", "{" + inner + "}", outResp, nullptr, outErr);
    }

    bool callWindowPin(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.window.pin", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callWindowWaggle(const std::string& sessionId, std::string& outResp, DWORD* outErr = nullptr)
    {
        return call("seer.window.waggle", "{" + detail::appendOptionalSessionId(escapeJson(sessionId)) + "}", outResp,
                    nullptr, outErr);
    }

    bool callWindowSetBounds(const std::string& sessionId,
                             const RECT& rect,
                             std::string& outResp,
                             DWORD* outErr = nullptr)
    {
        const LONG w = rect.right - rect.left;
        const LONG h = rect.bottom - rect.top;
        std::string inner = R"("rect":{"x":)" + std::to_string(rect.left) + R"(,"y":)" + std::to_string(rect.top) +
                            R"(,"width":)" + std::to_string(w) + R"(,"height":)" + std::to_string(h) + "}";
        if (!sessionId.empty()) {
            inner = detail::appendOptionalSessionId(escapeJson(sessionId)) + "," + inner;
        }
        return call("seer.window.setBounds", "{" + inner + "}", outResp, nullptr, outErr);
    }

    bool callWindowSetFullscreen(const std::string& sessionId,
                                 bool fullscreen,
                                 std::string& outResp,
                                 DWORD* outErr = nullptr)
    {
        std::string inner = std::string(R"("fullscreen":)") + (fullscreen ? "true" : "false");
        if (!sessionId.empty()) {
            inner = detail::appendOptionalSessionId(escapeJson(sessionId)) + "," + inner;
        }
        return call("seer.window.setFullscreen", "{" + inner + "}", outResp, nullptr, outErr);
    }

    bool callWindowSetInspector(const std::string& sessionId,
                                bool visible,
                                std::string& outResp,
                                DWORD* outErr = nullptr)
    {
        std::string inner = std::string(R"("visible":)") + (visible ? "true" : "false");
        if (!sessionId.empty()) {
            inner = detail::appendOptionalSessionId(escapeJson(sessionId)) + "," + inner;
        }
        return call("seer.window.setInspector", "{" + inner + "}", outResp, nullptr, outErr);
    }

    bool callEventsSubscribe(const std::vector<std::string>& events,
                             std::string& outResp,
                             DWORD* outErr = nullptr)
    {
        return callEventsList("seer.events.subscribe", events, outResp, outErr);
    }

    bool callEventsUnsubscribe(const std::vector<std::string>& events,
                               std::string& outResp,
                               DWORD* outErr = nullptr)
    {
        return callEventsList("seer.events.unsubscribe", events, outResp, outErr);
    }

    bool callExplorerRegister(HWND topHwnd,
                              const std::string& name,
                              std::string& outResp,
                              DWORD* outErr = nullptr)
    {
        const std::uint32_t rawHwnd = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(topHwnd));
        std::string params = R"({"topHwnd":)" + std::to_string(rawHwnd);
        if (!name.empty()) {
            params += R"(,"name":")" + escapeJson(name) + "\"";
        }
        params += "}";
        return call("seer.explorer.register", params, outResp, nullptr, outErr);
    }

    bool callExplorerUnregister(HWND topHwnd, std::string& outResp, DWORD* outErr = nullptr)
    {
        const std::uint32_t rawHwnd = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(topHwnd));
        return call("seer.explorer.unregister", R"({"topHwnd":)" + std::to_string(rawHwnd) + "}", outResp, nullptr,
                    outErr);
    }

    bool callExplorerSuspend(HWND topHwnd,
                             bool suspended,
                             std::string& outResp,
                             DWORD* outErr = nullptr)
    {
        const std::uint32_t rawHwnd = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(topHwnd));
        return call("seer.explorer.suspend",
                    R"({"topHwnd":)" + std::to_string(rawHwnd) + R"(,"suspended":)" +
                        (suspended ? "true" : "false") + "}",
                    outResp, nullptr, outErr);
    }

private:
    bool callEventsList(const std::string& method,
                        const std::vector<std::string>& events,
                        std::string& outResp,
                        DWORD* outErr)
    {
        std::string arr = "[";
        for (size_t i = 0; i < events.size(); ++i) {
            if (i > 0) {
                arr += ",";
            }
            arr += "\"" + escapeJson(events[i]) + "\"";
        }
        arr += "]";
        return call(method, R"({"events":)" + arr + "}", outResp, nullptr, outErr);
    }

    HANDLE m_pipe = INVALID_HANDLE_VALUE;
    bool m_overlapped = false;
    std::uint64_t m_nextId = 1;
    EventCallback m_onEvent;
};

}  // namespace seer::v2

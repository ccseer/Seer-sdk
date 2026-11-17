<#
.SYNOPSIS
    Seer Named Pipe JSON-RPC 2.0 Client (PowerShell)
.DESCRIPTION
    Connects to Seer's session-scoped Named Pipe (\\.\pipe\Seer.IPC.v2.<SessionId>)
    using explicit access mask 0x0012018B, Identification SQOS, and server session
    + owner SID verification before sending JSON-RPC requests.
.EXAMPLE
    .\SeerIpc.ps1 -Info
    .\SeerIpc.ps1 -Preview "C:\path\to\document.pdf" -Mode main
    .\SeerIpc.ps1 -List
    .\SeerIpc.ps1 -Close
    .\SeerIpc.ps1 -Pin
#>

param (
    [string]$Preview = "",
    [ValidateSet("main", "separated", "pinned", "standalone")]
    [string]$Mode = "main",
    [string]$SessionId = "",
    [switch]$Info,
    [switch]$List,
    [switch]$Close,
    [switch]$Pin,
    [string]$PipeName = ""
)

$definition = @"
using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

public static class SeerPipeWin32
{
    private const uint DESIRED_ACCESS = 0x0012018B; // omits FILE_CREATE_PIPE_INSTANCE (0x4)
    private const uint OPEN_EXISTING = 3;
    private const uint SECURITY_SQOS_PRESENT = 0x00100000;
    private const uint SECURITY_IDENTIFICATION = 0x00010000;
    private const uint PIPE_READMODE_MESSAGE = 0x00000002;
    private const uint ERROR_MORE_DATA = 234;
    private const int SE_KERNEL_OBJECT = 6;
    private const uint OWNER_SECURITY_INFORMATION = 0x00000001;
    private const uint TOKEN_QUERY = 0x0008;
    private const int TokenUser = 1;
    private const int WinLocalSystemSid = 22;
    private const int WinBuiltinAdministratorsSid = 26;
    private const int MAX_FRAME = 64 * 1024;
    private static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern uint GetCurrentProcessId();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr GetCurrentProcess();

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ProcessIdToSessionId(uint dwProcessId, out uint pSessionId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool GetNamedPipeServerSessionId(IntPtr Pipe, out uint ServerSessionId);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern IntPtr CreateFileW(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool SetNamedPipeHandleState(
        IntPtr hNamedPipe, ref uint lpMode, IntPtr lpMaxCollectionCount, IntPtr lpCollectDataTimeout);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteFile(
        IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToWrite, out uint lpNumberOfBytesWritten, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool ReadFile(
        IntPtr hFile, byte[] lpBuffer, uint nNumberOfBytesToRead, out uint lpNumberOfBytesRead, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr hObject);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LocalFree(IntPtr hMem);

    [DllImport("advapi32.dll")]
    private static extern uint GetSecurityInfo(
        IntPtr handle, int ObjectType, uint SecurityInfo,
        out IntPtr ppsidOwner, IntPtr ppsidGroup, IntPtr ppDacl, IntPtr ppSacl, out IntPtr ppSecurityDescriptor);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool OpenProcessToken(IntPtr ProcessHandle, uint DesiredAccess, out IntPtr TokenHandle);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool GetTokenInformation(
        IntPtr TokenHandle, int TokenInformationClass, IntPtr TokenInformation, uint TokenInformationLength, out uint ReturnLength);

    [DllImport("advapi32.dll", SetLastError = true)]
    private static extern bool CreateWellKnownSid(
        int WellKnownSidType, IntPtr DomainSid, byte[] pSid, ref uint cbSid);

    [DllImport("advapi32.dll")]
    private static extern bool EqualSid(IntPtr pSid1, IntPtr pSid2);

    [DllImport("advapi32.dll")]
    private static extern bool EqualSid(IntPtr pSid1, byte[] pSid2);

    public static uint CurrentSessionId()
    {
        uint sid;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), out sid))
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        return sid;
    }

    private static void VerifyPipeServer(IntPtr handle, uint expectedSessionId)
    {
        uint serverSessionId;
        if (!GetNamedPipeServerSessionId(handle, out serverSessionId))
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }
        if (serverSessionId != expectedSessionId)
        {
            throw new InvalidOperationException("Pipe server session mismatch.");
        }

        IntPtr ownerSid;
        IntPtr sd;
        uint secErr = GetSecurityInfo(handle, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
                                      out ownerSid, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out sd);
        if (secErr != 0)
        {
            throw new System.ComponentModel.Win32Exception((int)secErr);
        }

        IntPtr token = IntPtr.Zero;
        IntPtr tokenBuf = IntPtr.Zero;
        try
        {
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, out token))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            uint needed = 0;
            GetTokenInformation(token, TokenUser, IntPtr.Zero, 0, out needed);
            tokenBuf = Marshal.AllocHGlobal((int)needed);
            if (!GetTokenInformation(token, TokenUser, tokenBuf, needed, out needed))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            IntPtr userSid = Marshal.ReadIntPtr(tokenBuf);

            byte[] adminSid = new byte[68];
            uint adminLen = (uint)adminSid.Length;
            if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, IntPtr.Zero, adminSid, ref adminLen))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }

            byte[] sysSid = new byte[68];
            uint sysLen = (uint)sysSid.Length;
            if (!CreateWellKnownSid(WinLocalSystemSid, IntPtr.Zero, sysSid, ref sysLen))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }

            if (!EqualSid(ownerSid, userSid) && !EqualSid(ownerSid, adminSid) && !EqualSid(ownerSid, sysSid))
            {
                throw new UnauthorizedAccessException("Untrusted pipe server owner SID.");
            }
        }
        finally
        {
            if (tokenBuf != IntPtr.Zero) Marshal.FreeHGlobal(tokenBuf);
            if (token != IntPtr.Zero) CloseHandle(token);
            if (sd != IntPtr.Zero) LocalFree(sd);
        }
    }

    public static string SendRpc(string pipeNameOverride, string jsonLine)
    {
        uint sessionId = CurrentSessionId();
        string pipeName = string.IsNullOrEmpty(pipeNameOverride)
            ? ("\\\\.\\pipe\\Seer.IPC.v2." + sessionId.ToString())
            : pipeNameOverride;

        IntPtr hPipe = CreateFileW(pipeName, DESIRED_ACCESS, 0, IntPtr.Zero, OPEN_EXISTING,
                                   SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, IntPtr.Zero);
        if (hPipe == INVALID_HANDLE_VALUE || hPipe == IntPtr.Zero)
        {
            throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
        }

        try
        {
            VerifyPipeServer(hPipe, sessionId);
            uint mode = PIPE_READMODE_MESSAGE;
            if (!SetNamedPipeHandleState(hPipe, ref mode, IntPtr.Zero, IntPtr.Zero))
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }

            byte[] frame = Encoding.UTF8.GetBytes(jsonLine + "\n");
            uint written;
            if (!WriteFile(hPipe, frame, (uint)frame.Length, out written, IntPtr.Zero) || written != frame.Length)
            {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }

            using (var ms = new MemoryStream())
            {
                byte[] buf = new byte[MAX_FRAME];
                while (true)
                {
                    uint read;
                    bool ok = ReadFile(hPipe, buf, (uint)buf.Length, out read, IntPtr.Zero);
                    if (read > 0)
                    {
                        ms.Write(buf, 0, (int)read);
                    }
                    if (ok)
                    {
                        break;
                    }
                    uint err = (uint)Marshal.GetLastWin32Error();
                    if (err == ERROR_MORE_DATA)
                    {
                        if (ms.Length > MAX_FRAME)
                        {
                            throw new InvalidDataException("Frame exceeds 64 KiB limit.");
                        }
                        continue;
                    }
                    throw new System.ComponentModel.Win32Exception((int)err);
                }
                return Encoding.UTF8.GetString(ms.ToArray()).Trim();
            }
        }
        finally
        {
            CloseHandle(hPipe);
        }
    }
}
"@

if (-not ([System.Management.Automation.PSTypeName]'SeerPipeWin32').Type) {
    Add-Type -TypeDefinition $definition
}

function Invoke-SeerRpc([string]$Method, [hashtable]$Params) {
    $req = @{
        jsonrpc = "2.0"
        id      = 1
        method  = $Method
        params  = $Params
    } | ConvertTo-Json -Compress -Depth 5
    $raw = [SeerPipeWin32]::SendRpc($PipeName, $req)
    return $raw
}

if ($Info) {
    Write-Output (Invoke-SeerRpc "seer.getInfo" @{})
}
elseif ($List) {
    Write-Output (Invoke-SeerRpc "seer.preview.list" @{})
}
elseif ($Preview) {
    $fullPath = [System.IO.Path]::GetFullPath($Preview)
    Write-Output (Invoke-SeerRpc "seer.preview.open" @{ path = $fullPath; mode = $Mode })
}
elseif ($Close) {
    $p = @{}
    if ($SessionId) { $p.sessionId = $SessionId }
    Write-Output (Invoke-SeerRpc "seer.preview.close" $p)
}
elseif ($Pin) {
    $p = @{}
    if ($SessionId) { $p.sessionId = $SessionId }
    Write-Output (Invoke-SeerRpc "seer.window.pin" $p)
}
else {
    Write-Output (Invoke-SeerRpc "seer.getInfo" @{})
}

// Requires .NET Core 3.0+ / .NET 5+ (uses System.Text.Json).
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace SeerExample
{
    /// <summary>
    /// Seer Named Pipe JSON-RPC 2.0 client in C#.
    /// Connects to \\.\pipe\Seer.IPC.v2.<SessionId> with 0x0012018B and Identification SQOS,
    /// verifies server session ID and owner SID, and executes synchronous JSON-RPC calls.
    ///
    /// For event subscriptions (seer.events.subscribe), open with FILE_FLAG_OVERLAPPED and
    /// read on a dedicated background thread so ReadFile does not block WriteFile.
    /// </summary>
    public sealed class SeerClient : IDisposable
    {
        private const string PipePrefix = @"\\.\pipe\Seer.IPC.v2.";
        private const int MaxFrameBytes = 64 * 1024;

        // Server DACL omits FILE_CREATE_PIPE_INSTANCE (0x4), so GENERIC_WRITE fails.
        private const uint DesiredAccess = 0x0012018B;
        private const uint OpenExisting = 3;
        private const uint SecuritySqosPresent = 0x00100000;
        private const uint SecurityIdentification = 0x00010000;
        private const uint PipeReadModeMessage = 0x00000002;
        private const uint ErrorMoreData = 234;

        private const int SeKernelObject = 6;
        private const uint OwnerSecurityInformation = 0x00000001;
        private const uint TokenQuery = 0x0008;
        private const int TokenUser = 1;
        private const int WinLocalSystemSid = 22;
        private const int WinBuiltinAdministratorsSid = 26;

        private static readonly IntPtr InvalidHandleValue = new IntPtr(-1);
        private IntPtr _pipe = InvalidHandleValue;
        private int _nextId = 1;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint GetCurrentProcessId();

        [DllImport("kernel32.dll")]
        private static extern IntPtr GetCurrentProcess();

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool ProcessIdToSessionId(uint dwProcessId, out uint pSessionId);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetNamedPipeServerSessionId(IntPtr pipe, out uint serverSessionId);

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
            IntPtr handle, int objectType, uint securityInfo,
            out IntPtr ppsidOwner, IntPtr ppsidGroup, IntPtr ppDacl, IntPtr ppSacl, out IntPtr ppSecurityDescriptor);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool OpenProcessToken(IntPtr processHandle, uint desiredAccess, out IntPtr tokenHandle);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool GetTokenInformation(
            IntPtr tokenHandle, int tokenInformationClass, IntPtr tokenInformation, uint tokenInformationLength, out uint returnLength);

        [DllImport("advapi32.dll", SetLastError = true)]
        private static extern bool CreateWellKnownSid(
            int wellKnownSidType, IntPtr domainSid, byte[] pSid, ref uint cbSid);

        [DllImport("advapi32.dll")]
        private static extern bool EqualSid(IntPtr pSid1, IntPtr pSid2);

        [DllImport("advapi32.dll")]
        private static extern bool EqualSid(IntPtr pSid1, byte[] pSid2);

        public static SeerClient Connect(string? pipeNameOverride = null)
        {
            if (!ProcessIdToSessionId(GetCurrentProcessId(), out uint sessionId))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            string pipeName = string.IsNullOrEmpty(pipeNameOverride)
                ? $"{PipePrefix}{sessionId}"
                : pipeNameOverride;

            IntPtr handle = CreateFileW(pipeName, DesiredAccess, 0, IntPtr.Zero, OpenExisting,
                                        SecuritySqosPresent | SecurityIdentification, IntPtr.Zero);
            if (handle == InvalidHandleValue || handle == IntPtr.Zero)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            try
            {
                VerifyPipeServer(handle, sessionId);
                uint mode = PipeReadModeMessage;
                if (!SetNamedPipeHandleState(handle, ref mode, IntPtr.Zero, IntPtr.Zero))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                return new SeerClient(handle);
            }
            catch
            {
                CloseHandle(handle);
                throw;
            }
        }

        private SeerClient(IntPtr pipe) => _pipe = pipe;

        public void Dispose()
        {
            if (_pipe != InvalidHandleValue && _pipe != IntPtr.Zero)
            {
                CloseHandle(_pipe);
                _pipe = InvalidHandleValue;
            }
        }

        private static void VerifyPipeServer(IntPtr handle, uint expectedSessionId)
        {
            if (!GetNamedPipeServerSessionId(handle, out uint serverSessionId))
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            if (serverSessionId != expectedSessionId)
            {
                throw new InvalidOperationException($"Pipe server session mismatch: {serverSessionId} != {expectedSessionId}");
            }

            uint secErr = GetSecurityInfo(handle, SeKernelObject, OwnerSecurityInformation,
                                          out IntPtr ownerSid, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero, out IntPtr sd);
            if (secErr != 0)
            {
                throw new Win32Exception((int)secErr);
            }

            IntPtr token = IntPtr.Zero;
            IntPtr tokenBuf = IntPtr.Zero;
            try
            {
                if (!OpenProcessToken(GetCurrentProcess(), TokenQuery, out token))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                GetTokenInformation(token, TokenUser, IntPtr.Zero, 0, out uint needed);
                tokenBuf = Marshal.AllocHGlobal((int)needed);
                if (!GetTokenInformation(token, TokenUser, tokenBuf, needed, out needed))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }
                IntPtr userSid = Marshal.ReadIntPtr(tokenBuf);

                byte[] adminSid = new byte[68];
                uint adminLen = (uint)adminSid.Length;
                if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, IntPtr.Zero, adminSid, ref adminLen))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                byte[] systemSid = new byte[68];
                uint systemLen = (uint)systemSid.Length;
                if (!CreateWellKnownSid(WinLocalSystemSid, IntPtr.Zero, systemSid, ref systemLen))
                {
                    throw new Win32Exception(Marshal.GetLastWin32Error());
                }

                if (!EqualSid(ownerSid, userSid) && !EqualSid(ownerSid, adminSid) && !EqualSid(ownerSid, systemSid))
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

        public JsonDocument Call(string method, Dictionary<string, object?>? @params = null)
        {
            var payload = new Dictionary<string, object?>
            {
                ["jsonrpc"] = "2.0",
                ["id"] = _nextId++,
                ["method"] = method,
                ["params"] = @params ?? new Dictionary<string, object?>()
            };

            byte[] frame = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(payload) + "\n");
            if (!WriteFile(_pipe, frame, (uint)frame.Length, out uint written, IntPtr.Zero) || written != frame.Length)
            {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }

            using var ms = new MemoryStream();
            byte[] buf = new byte[MaxFrameBytes];
            while (true)
            {
                bool ok = ReadFile(_pipe, buf, (uint)buf.Length, out uint read, IntPtr.Zero);
                if (read > 0)
                {
                    ms.Write(buf, 0, (int)read);
                }
                if (ok)
                {
                    break;
                }
                uint err = (uint)Marshal.GetLastWin32Error();
                if (err == ErrorMoreData)
                {
                    if (ms.Length > MaxFrameBytes)
                    {
                        throw new InvalidDataException("Frame exceeds 64 KiB limit.");
                    }
                    continue;
                }
                throw new Win32Exception((int)err);
            }

            return JsonDocument.Parse(ms.ToArray());
        }
    }

    internal static class Program
    {
        private static void Main(string[] args)
        {
            Console.WriteLine("=== Seer C# Named Pipe IPC Demo ===");
            using var client = SeerClient.Connect();

            using var info = client.Call("seer.getInfo");
            Console.WriteLine($"seer.getInfo: {info.RootElement}");

            if (args.Length > 0)
            {
                string absPath = Path.GetFullPath(args[0]);
                using var openRes = client.Call("seer.preview.open", new Dictionary<string, object?>
                {
                    ["path"] = absPath,
                    ["mode"] = "main",
                    ["ifAlreadyOpen"] = "noop"
                });
                Console.WriteLine($"seer.preview.open: {openRes.RootElement}");
            }
            else
            {
                using var listRes = client.Call("seer.preview.list");
                Console.WriteLine($"seer.preview.list: {listRes.RootElement}");
                Console.WriteLine("Usage: SeerExample.exe <file_path_to_preview>");
            }
        }
    }
}

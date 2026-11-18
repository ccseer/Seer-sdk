using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

namespace SeerExample
{
    public enum SeerCommand : int
    {
        RequestPath = 4000,
        ResponsePath = 4001,
        InvokeW32 = 5000,
        InvokeW32Separated = 5001,
        IsVisible = 5004,
        Hide = 5005,
        GetCurrentFile = 5006,
        GetVersion = 5007,
        IsTrackingEnabled = 5008,
        InvokeW32Standalone = 5009,
        InvokeW32Pinned = 5010
    }

    /// <summary>
    /// Seer IPC client implementation in pure C# using Win32 API.
    /// </summary>
    public static class SeerClient
    {
        public const string WindowClassName = "SeerWindowClass";
        private const int WM_COPYDATA = 0x004A;

        [StructLayout(LayoutKind.Sequential)]
        private struct COPYDATASTRUCT
        {
            public IntPtr dwData;
            public int cbData;
            public IntPtr lpData;
        }

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr FindWindow(string? lpClassName, string? lpWindowName);

        [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr hWnd, int Msg, IntPtr wParam, ref COPYDATASTRUCT lParam);

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentProcessId();

        [DllImport("kernel32.dll")]
        private static extern ulong GetTickCount64();

        /// <summary>
        /// Finds the running Seer main window handle.
        /// </summary>
        public static IntPtr FindSeerWindow()
        {
            return FindWindow(WindowClassName, null);
        }

        /// <summary>
        /// Checks if Seer is currently running.
        /// </summary>
        public static bool IsRunning()
        {
            return FindSeerWindow() != IntPtr.Zero;
        }

        /// <summary>
        /// Sends a command with no payload.
        /// </summary>
        public static IntPtr SendSimpleCommand(SeerCommand command)
        {
            IntPtr hwnd = FindSeerWindow();
            if (hwnd == IntPtr.Zero) return IntPtr.Zero;

            COPYDATASTRUCT cds = new COPYDATASTRUCT
            {
                dwData = (IntPtr)(int)command,
                cbData = 0,
                lpData = IntPtr.Zero
            };
            return SendMessage(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds);
        }

        /// <summary>
        /// Sends a command with a string payload (Unicode / UTF-16).
        /// </summary>
        public static bool SendPathCommand(SeerCommand command, string path)
        {
            if (string.IsNullOrEmpty(path)) return false;

            IntPtr hwnd = FindSeerWindow();
            if (hwnd == IntPtr.Zero) return false;

            string fullPath;
            try
            {
                fullPath = System.IO.Path.GetFullPath(path);
            }
            catch
            {
                fullPath = path;
            }

            IntPtr ptrString = Marshal.StringToHGlobalUni(fullPath);
            try
            {
                COPYDATASTRUCT cds = new COPYDATASTRUCT
                {
                    dwData = (IntPtr)(int)command,
                    cbData = (fullPath.Length + 1) * sizeof(char),
                    lpData = ptrString
                };
                return SendMessage(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds) != IntPtr.Zero;
            }
            finally
            {
                Marshal.FreeHGlobal(ptrString);
            }
        }

        /// <summary>
        /// Triggers preview for the specified file.
        /// Note: The interval between invocations should be at least 200ms.
        /// </summary>
        public static bool PreviewFile(string filePath)
        {
            return SendPathCommand(SeerCommand.InvokeW32, filePath);
        }

        /// <summary>
        /// Triggers preview for the specified file in a separated (standalone) window.
        /// </summary>
        public static bool PreviewFileSeparated(string filePath)
        {
            return SendPathCommand(SeerCommand.InvokeW32Separated, filePath);
        }

        /// <summary>
        /// Triggers preview for the specified file in a standalone viewer process (SeerViewer.exe).
        /// </summary>
        public static bool PreviewFileStandalone(string filePath)
        {
            return SendPathCommand(SeerCommand.InvokeW32Standalone, filePath);
        }

        /// <summary>
        /// Triggers preview for the specified file and pins the window (always on top).
        /// </summary>
        public static bool PreviewFilePinned(string filePath)
        {
            return SendPathCommand(SeerCommand.InvokeW32Pinned, filePath);
        }

        /// <summary>
        /// Hides the Seer main window if currently visible.
        /// </summary>
        public static void Hide()
        {
            SendSimpleCommand(SeerCommand.Hide);
        }

        /// <summary>
        /// Checks if Seer main window is visible.
        /// </summary>
        public static bool IsVisible()
        {
            return SendSimpleCommand(SeerCommand.IsVisible) != IntPtr.Zero;
        }

        /// <summary>
        /// Queries whether file tracking mode is enabled in Seer.
        /// </summary>
        public static bool IsTrackingEnabled()
        {
            return SendSimpleCommand(SeerCommand.IsTrackingEnabled) != IntPtr.Zero;
        }

        /// <summary>
        /// Requests the Seer version integer (e.g., 40001 -> 4.0.1).
        /// </summary>
        public static int GetVersion()
        {
            return (int)SendSimpleCommand(SeerCommand.GetVersion);
        }

        /// <summary>
        /// Requests the file path currently previewed in Seer via shared memory.
        /// </summary>
        public static string? GetCurrentFile()
        {
            IntPtr hwnd = FindSeerWindow();
            if (hwnd == IntPtr.Zero) return null;

            string mapName = $"SeerCurFile_{GetCurrentProcessId()}_{GetTickCount64()}";
            const int maxPathChars = 260;
            const int bufBytes = maxPathChars * sizeof(char);

            try
            {
                using var mmf = MemoryMappedFile.CreateNew(mapName, bufBytes);
                using var accessor = mmf.CreateViewAccessor(0, bufBytes);

                // Initialize first wchar to 0
                accessor.Write(0, '\0');

                IntPtr ptrMapName = Marshal.StringToHGlobalUni(mapName);
                try
                {
                    COPYDATASTRUCT cds = new COPYDATASTRUCT
                    {
                        dwData = (IntPtr)(int)SeerCommand.GetCurrentFile,
                        cbData = bufBytes,
                        lpData = ptrMapName
                    };
                    SendMessage(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds);

                    char[] buffer = new char[maxPathChars];
                    accessor.ReadArray(0, buffer, 0, maxPathChars);
                    int nullIndex = Array.IndexOf(buffer, '\0');
                    string result = nullIndex >= 0 ? new string(buffer, 0, nullIndex) : new string(buffer);
                    return string.IsNullOrEmpty(result) ? null : result;
                }
                finally
                {
                    Marshal.FreeHGlobal(ptrMapName);
                }
            }
            catch
            {
                return null;
            }
        }
    }

    internal class Program
    {
        static void Main(string[] args)
        {
            Console.WriteLine("=== Seer C# IPC Demo ===");
            if (!SeerClient.IsRunning())
            {
                Console.WriteLine("Seer is not running! Please start Seer first.");
                return;
            }

            int version = SeerClient.GetVersion();
            Console.WriteLine($"Seer Version: {version}");
            Console.WriteLine($"Is Visible: {SeerClient.IsVisible()}");
            Console.WriteLine($"Is Tracking Enabled: {SeerClient.IsTrackingEnabled()}");

            if (args.Length > 0)
            {
                string targetFile = args[0];
                Console.WriteLine($"Invoking preview for: {targetFile}");
                SeerClient.PreviewFile(targetFile);
            }
            else
            {
                Console.WriteLine("Usage: SeerExample.exe <file_path_to_preview>");
            }
        }
    }
}

<#
.SYNOPSIS
    Seer IPC PowerShell Script
.DESCRIPTION
    Allows interacting with Seer from PowerShell using Win32 API.
.EXAMPLE
    .\SeerIpc.ps1 -Preview "C:\path\to\document.pdf"
    .\SeerIpc.ps1 -Hide
    .\SeerIpc.ps1 -Status
#>

param (
    [string]$Preview = "",
    [string]$PreviewSeparated = "",
    [string]$PreviewStandalone = "",
    [string]$PreviewPinned = "",
    [switch]$Hide,
    [switch]$Status,
    [switch]$GetVersion
)

$definition = @"
using System;
using System.Runtime.InteropServices;

public static class SeerWin32
{
    public const string ClassName = "SeerWindowClass";
    public const int WM_COPYDATA = 0x004A;

    [StructLayout(LayoutKind.Sequential)]
    public struct COPYDATASTRUCT
    {
        public IntPtr dwData;
        public int cbData;
        public IntPtr lpData;
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hWnd, int Msg, IntPtr wParam, ref COPYDATASTRUCT lParam);

    public static IntPtr FindSeer()
    {
        return FindWindow(ClassName, null);
    }

    public static bool SendPath(int command, string path)
    {
        IntPtr hwnd = FindSeer();
        if (hwnd == IntPtr.Zero || string.IsNullOrEmpty(path)) return false;

        IntPtr ptr = Marshal.StringToHGlobalUni(path);
        try
        {
            COPYDATASTRUCT cds = new COPYDATASTRUCT
            {
                dwData = (IntPtr)command,
                cbData = (path.Length + 1) * sizeof(char),
                lpData = ptr
            };
            return SendMessage(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds) != IntPtr.Zero;
        }
        finally
        {
            Marshal.FreeHGlobal(ptr);
        }
    }

    public static IntPtr SendSimple(int command)
    {
        IntPtr hwnd = FindSeer();
        if (hwnd == IntPtr.Zero) return IntPtr.Zero;

        COPYDATASTRUCT cds = new COPYDATASTRUCT
        {
            dwData = (IntPtr)command,
            cbData = 0,
            lpData = IntPtr.Zero
        };
        return SendMessage(hwnd, WM_COPYDATA, IntPtr.Zero, ref cds);
    }
}
"@

if (-not ([System.Management.Automation.PSTypeName]'SeerWin32').Type) {
    Add-Type -TypeDefinition $definition
}

$hwnd = [SeerWin32]::FindSeer()
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "[!] Seer is not running." -ForegroundColor Yellow
}

function Get-FullPathSafely([string]$path) {
    try {
        return (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
    } catch {
        return [System.IO.Path]::GetFullPath($path)
    }
}

if ($Preview) {
    $fullPath = Get-FullPathSafely $Preview
    Write-Host "[*] Previewing: $fullPath"
    [SeerWin32]::SendPath(5000, $fullPath) | Out-Null
}
elseif ($PreviewSeparated) {
    $fullPath = Get-FullPathSafely $PreviewSeparated
    Write-Host "[*] Previewing (separated): $fullPath"
    [SeerWin32]::SendPath(5001, $fullPath) | Out-Null
}
elseif ($PreviewStandalone) {
    $fullPath = Get-FullPathSafely $PreviewStandalone
    Write-Host "[*] Previewing (standalone): $fullPath"
    [SeerWin32]::SendPath(5009, $fullPath) | Out-Null
}
elseif ($PreviewPinned) {
    $fullPath = Get-FullPathSafely $PreviewPinned
    Write-Host "[*] Previewing (pinned): $fullPath"
    [SeerWin32]::SendPath(5010, $fullPath) | Out-Null
}
elseif ($Hide) {
    Write-Host "[*] Hiding Seer preview window..."
    [SeerWin32]::SendSimple(5005) | Out-Null
}
elseif ($GetVersion) {
    $ver = [SeerWin32]::SendSimple(5007)
    Write-Host "Seer Version: $ver"
}
elseif ($Status) {
    $visible = [SeerWin32]::SendSimple(5004) -ne [IntPtr]::Zero
    $tracking = [SeerWin32]::SendSimple(5008) -ne [IntPtr]::Zero
    $ver = [SeerWin32]::SendSimple(5007)
    Write-Host "Seer Status:"
    Write-Host "  Running:          $($hwnd -ne [IntPtr]::Zero)"
    Write-Host "  Version:          $ver"
    Write-Host "  Window Visible:   $visible"
    Write-Host "  Tracking Mode:    $tracking"
}
else {
    Write-Host "Seer PowerShell IPC Helper"
    Write-Host "Usage:"
    Write-Host "  .\SeerIpc.ps1 -Preview <file_path>"
    Write-Host "  .\SeerIpc.ps1 -PreviewSeparated <file_path>"
    Write-Host "  .\SeerIpc.ps1 -PreviewStandalone <file_path>"
    Write-Host "  .\SeerIpc.ps1 -PreviewPinned <file_path>"
    Write-Host "  .\SeerIpc.ps1 -Hide"
    Write-Host "  .\SeerIpc.ps1 -Status"
}

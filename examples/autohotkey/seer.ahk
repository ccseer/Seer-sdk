#Requires AutoHotkey v2.0
; ==============================================================================
; Seer AutoHotkey (v2) IPC Helper
; Control Seer file previews from AutoHotkey scripts or hotkeys.
; ==============================================================================

global SEER_CLASS_NAME := "SeerWindowClass"
global WM_COPYDATA      := 0x004A

global SEER_INVOKE_W32     := 5000
global SEER_INVOKE_W32_SEP := 5001
global SEER_IS_VISIBLE     := 5004
global SEER_HIDE           := 5005
global SEER_GET_VERSION    := 5007
global SEER_IS_TRACKING    := 5008

; Find Seer window handle
SeerFindWindow() {
    return WinExist("ahk_class " . SEER_CLASS_NAME)
}

; Check if Seer is currently running
SeerIsRunning() {
    return SeerFindWindow() != 0
}

; Send a string path command (e.g. preview)
SeerSendPath(command, filePath) {
    hwnd := SeerFindWindow()
    if (!hwnd || filePath == "")
        return false

    ; Resolve relative path to full path if file exists
    if (FileExist(filePath)) {
        Loop Files filePath
            filePath := A_LoopFileFullPath
    }

    ; Build COPYDATASTRUCT buffer
    ; Win32 struct: { ULONG_PTR dwData (offset 0); DWORD cbData (offset A_PtrSize); PVOID lpData (offset A_PtrSize * 2); }
    ; Note: on 64-bit Windows, pointer alignment requires lpData to be at offset 16 (A_PtrSize * 2),
    ; and the total struct size is 24 bytes (A_PtrSize * 3).
    sizeInBytes := (StrLen(filePath) + 1) * 2
    pathBuf := Buffer(sizeInBytes)
    StrPut(filePath, pathBuf, "UTF-16")

    cds := Buffer(A_PtrSize * 3, 0)
    NumPut("UPtr", command, cds, 0)
    NumPut("UInt", sizeInBytes, cds, A_PtrSize)
    NumPut("UPtr", pathBuf.Ptr, cds, A_PtrSize * 2)

    return SendMessage(WM_COPYDATA, 0, cds.Ptr, , "ahk_id " . hwnd)
}

; Send a simple command with no payload
SeerSendSimple(command) {
    hwnd := SeerFindWindow()
    if (!hwnd)
        return 0

    cds := Buffer(A_PtrSize * 3, 0)
    NumPut("UPtr", command, cds, 0)
    NumPut("UInt", 0, cds, A_PtrSize)
    NumPut("UPtr", 0, cds, A_PtrSize * 2)

    return SendMessage(WM_COPYDATA, 0, cds.Ptr, , "ahk_id " . hwnd)
}

; Trigger preview for a file
SeerPreview(filePath) {
    return SeerSendPath(SEER_INVOKE_W32, filePath)
}

; Trigger preview in separated window
SeerPreviewSeparated(filePath) {
    return SeerSendPath(SEER_INVOKE_W32_SEP, filePath)
}

; Hide preview window
SeerHide() {
    return SeerSendSimple(SEER_HIDE)
}

; Check if main window is visible
SeerIsVisible() {
    return SeerSendSimple(SEER_IS_VISIBLE) != 0
}

; Get version integer (e.g. 40001)
SeerGetVersion() {
    return SeerSendSimple(SEER_GET_VERSION)
}

; ==============================================================================
; Example Hotkey Usage:
; Press Win + Space to preview the selected file in Windows Explorer
; ==============================================================================
#Space::
{
    ; Attempt to copy selected file in Explorer
    A_Clipboard := ""
    Send("^c")
    if (ClipWait(0.5, 1)) {
        ; A_Clipboard may contain multiple files separated by newlines; take the first one
        clipText := Trim(A_Clipboard, "`r`n`"")
        selectedFile := StrSplit(clipText, "`n", "`r")[1]
        selectedFile := Trim(selectedFile, "`"")
        if (FileExist(selectedFile)) {
            SeerPreview(selectedFile)
            return
        }
    }
    MsgBox("Please select a valid file to preview with Seer.", "Seer AHK")
}

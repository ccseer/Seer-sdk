package main

import (
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"unsafe"
)

const (
	SeerClassName = "SeerWindowClass"
	WmCopyData    = 0x004A

	SeerInvokeW32       = 5000
	SeerInvokeW32Sep    = 5001
	SeerIsVisible       = 5004
	SeerHide            = 5005
	SeerGetCurrentFile  = 5006
	SeerGetVersion      = 5007
	SeerIsTracking      = 5008
)

type CopyDataStruct struct {
	DwData uintptr
	CbData uint32
	LpData uintptr
}

var (
	modUser32       = syscall.NewLazyDLL("user32.dll")
	procFindWindowW = modUser32.NewProc("FindWindowW")
	procSendMessage = modUser32.NewProc("SendMessageW")
)

func findSeerWindow() uintptr {
	className, _ := syscall.UTF16PtrFromString(SeerClassName)
	hwnd, _, _ := procFindWindowW.Call(
		uintptr(unsafe.Pointer(className)),
		0,
	)
	return hwnd
}

func sendSimpleCommand(command int) uintptr {
	hwnd := findSeerWindow()
	if hwnd == 0 {
		return 0
	}

	cds := CopyDataStruct{
		DwData: uintptr(command),
		CbData: 0,
		LpData: 0,
	}

	ret, _, _ := procSendMessage.Call(
		hwnd,
		WmCopyData,
		0,
		uintptr(unsafe.Pointer(&cds)),
	)
	return ret
}

func sendPathCommand(command int, filePath string) bool {
	hwnd := findSeerWindow()
	if hwnd == 0 || filePath == "" {
		return false
	}

	absPath, err := filepath.Abs(filePath)
	if err != nil {
		absPath = filePath
	}

	utf16Chars, err := syscall.UTF16FromString(absPath)
	if err != nil {
		return false
	}

	cds := CopyDataStruct{
		DwData: uintptr(command),
		CbData: uint32(len(utf16Chars) * 2),
		LpData: uintptr(unsafe.Pointer(&utf16Chars[0])),
	}

	ret, _, _ := procSendMessage.Call(
		hwnd,
		WmCopyData,
		0,
		uintptr(unsafe.Pointer(&cds)),
	)
	return ret != 0
}

func PreviewFile(filePath string) bool {
	return sendPathCommand(SeerInvokeW32, filePath)
}

func PreviewFileSeparated(filePath string) bool {
	return sendPathCommand(SeerInvokeW32Sep, filePath)
}

func HidePreview() {
	sendSimpleCommand(SeerHide)
}

func IsVisible() bool {
	return sendSimpleCommand(SeerIsVisible) != 0
}

func IsTrackingEnabled() bool {
	return sendSimpleCommand(SeerIsTracking) != 0
}

func GetVersion() int {
	return int(sendSimpleCommand(SeerGetVersion))
}

func main() {
	fmt.Println("=== Seer Go (Golang) IPC Demo ===")

	hwnd := findSeerWindow()
	if hwnd == 0 {
		fmt.Println("Seer is not running! Please start Seer first.")
		return
	}

	fmt.Printf("Seer Version: %d\n", GetVersion())
	fmt.Printf("Is Main Window Visible: %v\n", IsVisible())

	if len(os.Args) > 1 {
		targetPath := os.Args[1]
		fmt.Printf("Previewing: %s\n", targetPath)
		PreviewFile(targetPath)
	} else {
		fmt.Println("Usage: go run main.go <file_path_to_preview>")
	}
}

// Seer Named Pipe JSON-RPC 2.0 Client for Go (pure standard library)
// Works on Windows 10/11 (x64) without external packages.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"unsafe"
)

const (
	pipePrefix              = `\\.\pipe\Seer.IPC.v2.`
	maxFrameBytes           = 64 * 1024
	desiredAccess           = 0x0012018B // omits FILE_CREATE_PIPE_INSTANCE (0x4)
	openExisting            = 3
	securitySqosPresent     = 0x00100000
	securityIdentification  = 0x00010000
	pipeReadModeMessage     = 0x00000002
	errorMoreData           = 234
	seKernelObject          = 6
	ownerSecurityInfo       = 0x00000001
	tokenQuery              = 0x0008
	tokenUser               = 1
	winLocalSystemSid       = 22
	winBuiltinAdminSid      = 26
)

var (
	kernel32                    = syscall.NewLazyDLL("kernel32.dll")
	advapi32                    = syscall.NewLazyDLL("advapi32.dll")
	procGetCurrentProcessId     = kernel32.NewProc("GetCurrentProcessId")
	procGetCurrentProcess       = kernel32.NewProc("GetCurrentProcess")
	procProcessIdToSessionId    = kernel32.NewProc("ProcessIdToSessionId")
	procGetNamedPipeServerSess  = kernel32.NewProc("GetNamedPipeServerSessionId")
	procCreateFileW             = kernel32.NewProc("CreateFileW")
	procSetNamedPipeHandleState = kernel32.NewProc("SetNamedPipeHandleState")
	procWriteFile               = kernel32.NewProc("WriteFile")
	procReadFile                = kernel32.NewProc("ReadFile")
	procLocalFree               = kernel32.NewProc("LocalFree")
	procGetSecurityInfo         = advapi32.NewProc("GetSecurityInfo")
	procOpenProcessToken        = advapi32.NewProc("OpenProcessToken")
	procGetTokenInformation     = advapi32.NewProc("GetTokenInformation")
	procCreateWellKnownSid      = advapi32.NewProc("CreateWellKnownSid")
	procEqualSid                = advapi32.NewProc("EqualSid")
)

type sidAndAttributes struct {
	Sid        uintptr
	Attributes uint32
}

type tokenUserInfo struct {
	User sidAndAttributes
}

func currentSessionID() (uint32, error) {
	pid, _, _ := procGetCurrentProcessId.Call()
	var sid uint32
	r, _, err := procProcessIdToSessionId.Call(pid, uintptr(unsafe.Pointer(&sid)))
	if r == 0 {
		return 0, err
	}
	return sid, nil
}

func wellKnownSID(sidType uint32) ([]byte, error) {
	buf := make([]byte, 68)
	size := uint32(len(buf))
	r, _, err := procCreateWellKnownSid.Call(uintptr(sidType), 0, uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&size)))
	if r == 0 {
		return nil, err
	}
	return buf, nil
}

func equalSID(a, b uintptr) bool {
	r, _, _ := procEqualSid.Call(a, b)
	return r != 0
}

func verifyPipeServer(handle syscall.Handle, expectedSession uint32) error {
	var serverSession uint32
	r, _, err := procGetNamedPipeServerSess.Call(uintptr(handle), uintptr(unsafe.Pointer(&serverSession)))
	if r == 0 {
		return err
	}
	if serverSession != expectedSession {
		return fmt.Errorf("pipe server session mismatch: %d != %d", serverSession, expectedSession)
	}

	var ownerSid, sd uintptr
	secRet, _, _ := procGetSecurityInfo.Call(
		uintptr(handle), seKernelObject, ownerSecurityInfo,
		uintptr(unsafe.Pointer(&ownerSid)), 0, 0, 0, uintptr(unsafe.Pointer(&sd)),
	)
	if secRet != 0 {
		return syscall.Errno(secRet)
	}
	defer procLocalFree.Call(sd)

	curProc, _, _ := procGetCurrentProcess.Call()
	var tok syscall.Handle
	r, _, err = procOpenProcessToken.Call(curProc, tokenQuery, uintptr(unsafe.Pointer(&tok)))
	if r == 0 {
		return err
	}
	defer syscall.CloseHandle(tok)

	var needed uint32
	procGetTokenInformation.Call(uintptr(tok), tokenUser, 0, 0, uintptr(unsafe.Pointer(&needed)))
	tokBuf := make([]byte, needed)
	r, _, err = procGetTokenInformation.Call(uintptr(tok), tokenUser, uintptr(unsafe.Pointer(&tokBuf[0])), uintptr(needed), uintptr(unsafe.Pointer(&needed)))
	if r == 0 {
		return err
	}
	user := (*tokenUserInfo)(unsafe.Pointer(&tokBuf[0]))

	adminSID, err := wellKnownSID(winBuiltinAdminSid)
	if err != nil {
		return err
	}
	sysSID, err := wellKnownSID(winLocalSystemSid)
	if err != nil {
		return err
	}

	if !equalSID(ownerSid, user.User.Sid) &&
		!equalSID(ownerSid, uintptr(unsafe.Pointer(&adminSID[0]))) &&
		!equalSID(ownerSid, uintptr(unsafe.Pointer(&sysSID[0]))) {
		return fmt.Errorf("untrusted pipe server owner SID")
	}
	return nil
}

func connectSeerPipe() (syscall.Handle, error) {
	sessionID, err := currentSessionID()
	if err != nil {
		return syscall.InvalidHandle, err
	}
	namePtr, err := syscall.UTF16PtrFromString(fmt.Sprintf("%s%d", pipePrefix, sessionID))
	if err != nil {
		return syscall.InvalidHandle, err
	}
	raw, _, callErr := procCreateFileW.Call(
		uintptr(unsafe.Pointer(namePtr)),
		desiredAccess, 0, 0, openExisting,
		securitySqosPresent|securityIdentification, 0,
	)
	handle := syscall.Handle(raw)
	if handle == syscall.InvalidHandle || handle == 0 {
		return syscall.InvalidHandle, callErr
	}
	if err := verifyPipeServer(handle, sessionID); err != nil {
		syscall.CloseHandle(handle)
		return syscall.InvalidHandle, err
	}
	mode := uint32(pipeReadModeMessage)
	r, _, err := procSetNamedPipeHandleState.Call(uintptr(handle), uintptr(unsafe.Pointer(&mode)), 0, 0)
	if r == 0 {
		syscall.CloseHandle(handle)
		return syscall.InvalidHandle, err
	}
	return handle, nil
}

func sendRPC(handle syscall.Handle, id int, method string, params map[string]any) (string, error) {
	raw, err := json.Marshal(map[string]any{
		"jsonrpc": "2.0",
		"id":      id,
		"method":  method,
		"params":  params,
	})
	if err != nil {
		return "", err
	}
	raw = append(raw, '\n')
	var written uint32
	r, _, wErr := procWriteFile.Call(uintptr(handle), uintptr(unsafe.Pointer(&raw[0])), uintptr(len(raw)), uintptr(unsafe.Pointer(&written)), 0)
	if r == 0 || int(written) != len(raw) {
		return "", wErr
	}

	var accum bytes.Buffer
	buf := make([]byte, maxFrameBytes)
	for {
		var read uint32
		ok, _, rErr := procReadFile.Call(uintptr(handle), uintptr(unsafe.Pointer(&buf[0])), uintptr(len(buf)), uintptr(unsafe.Pointer(&read)), 0)
		if read > 0 {
			accum.Write(buf[:read])
		}
		if ok != 0 {
			break
		}
		if errno, isErrno := rErr.(syscall.Errno); isErrno && errno == errorMoreData {
			if accum.Len() > maxFrameBytes {
				return "", fmt.Errorf("frame exceeds 64 KiB limit")
			}
			continue
		}
		return "", rErr
	}
	return string(bytes.TrimSpace(accum.Bytes())), nil
}

func main() {
	handle, err := connectSeerPipe()
	if err != nil {
		fmt.Printf("Failed to connect to Seer pipe: %v\n", err)
		return
	}
	defer syscall.CloseHandle(handle)

	info, err := sendRPC(handle, 1, "seer.getInfo", map[string]any{})
	if err != nil {
		fmt.Printf("seer.getInfo error: %v\n", err)
		return
	}
	fmt.Println("seer.getInfo:", info)

	if len(os.Args) > 1 {
		absPath, err := filepath.Abs(os.Args[1])
		if err != nil {
			absPath = os.Args[1]
		}
		resp, err := sendRPC(handle, 2, "seer.preview.open", map[string]any{
			"path":          absPath,
			"mode":          "main",
			"ifAlreadyOpen": "noop",
		})
		if err != nil {
			fmt.Printf("seer.preview.open error: %v\n", err)
			return
		}
		fmt.Println("seer.preview.open:", resp)
	} else {
		listResp, err := sendRPC(handle, 2, "seer.preview.list", map[string]any{})
		if err != nil {
			fmt.Printf("seer.preview.list error: %v\n", err)
			return
		}
		fmt.Println("seer.preview.list:", listResp)
		fmt.Println("Usage: go run main.go <file_path_to_preview>")
	}
}

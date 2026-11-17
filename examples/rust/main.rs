//! Seer Named Pipe JSON-RPC 2.0 Client for Rust (zero external crates)
//! Uses raw Win32 FFI with 0x0012018B, Identification SQOS, and server session + owner SID checks.

use std::env;
use std::ffi::{c_void, OsStr};
use std::os::windows::ffi::OsStrExt;
use std::path::Path;

const PIPE_PREFIX: &str = r"\\.\pipe\Seer.IPC.v2.";
const MAX_FRAME_BYTES: usize = 64 * 1024;
const DESIRED_ACCESS: u32 = 0x0012_018B; // omits FILE_CREATE_PIPE_INSTANCE (0x4)
const OPEN_EXISTING: u32 = 3;
const SECURITY_SQOS_PRESENT: u32 = 0x0010_0000;
const SECURITY_IDENTIFICATION: u32 = 0x0001_0000;
const PIPE_READMODE_MESSAGE: u32 = 0x0000_0002;
const ERROR_MORE_DATA: u32 = 234;
const SE_KERNEL_OBJECT: i32 = 6;
const OWNER_SECURITY_INFORMATION: u32 = 0x0000_0001;
const TOKEN_QUERY: u32 = 0x0008;
const TOKEN_USER: i32 = 1;
const WIN_LOCAL_SYSTEM_SID: i32 = 22;
const WIN_BUILTIN_ADMINS_SID: i32 = 26;

type HANDLE = *mut c_void;
type PSID = *mut c_void;
const INVALID_HANDLE_VALUE: HANDLE = (-1isize) as HANDLE;

#[repr(C)]
struct TokenUser {
    user_sid: PSID,
    attributes: u32,
}

#[link(name = "kernel32")]
extern "system" {
    fn GetCurrentProcessId() -> u32;
    fn GetCurrentProcess() -> HANDLE;
    fn ProcessIdToSessionId(pid: u32, session_id: *mut u32) -> i32;
    fn GetNamedPipeServerSessionId(pipe: HANDLE, server_session_id: *mut u32) -> i32;
    fn CreateFileW(
        name: *const u16, access: u32, share: u32, sa: *const c_void,
        disp: u32, flags: u32, tmpl: HANDLE,
    ) -> HANDLE;
    fn SetNamedPipeHandleState(pipe: HANDLE, mode: *mut u32, max_coll: *mut u32, timeout: *mut u32) -> i32;
    fn WriteFile(pipe: HANDLE, buf: *const u8, len: u32, written: *mut u32, ov: *mut c_void) -> i32;
    fn ReadFile(pipe: HANDLE, buf: *mut u8, len: u32, read: *mut u32, ov: *mut c_void) -> i32;
    fn CloseHandle(h: HANDLE) -> i32;
    fn LocalFree(p: *mut c_void) -> *mut c_void;
    fn GetLastError() -> u32;
}

#[link(name = "advapi32")]
extern "system" {
    fn GetSecurityInfo(
        handle: HANDLE, obj_type: i32, sec_info: u32,
        owner: *mut PSID, group: *mut PSID, dacl: *mut *mut c_void, sacl: *mut *mut c_void, sd: *mut *mut c_void,
    ) -> u32;
    fn OpenProcessToken(proc_h: HANDLE, access: u32, token: *mut HANDLE) -> i32;
    fn GetTokenInformation(token: HANDLE, cls: i32, info: *mut u8, len: u32, ret_len: *mut u32) -> i32;
    fn CreateWellKnownSid(sid_type: i32, domain: PSID, sid: *mut u8, cb_sid: *mut u32) -> i32;
    fn EqualSid(sid1: PSID, sid2: PSID) -> i32;
}

fn to_wide_null(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(Some(0)).collect()
}

fn escape_json(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 8);
    for ch in s.chars() {
        match ch {
            '"' => out.push_str(r#"\""#),
            '\\' => out.push_str(r"\\"),
            '\n' => out.push_str(r"\n"),
            '\r' => out.push_str(r"\r"),
            '\t' => out.push_str(r"\t"),
            c if (c as u32) < 0x20 => out.push_str(&format!(r"\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out
}

unsafe fn verify_pipe_server(pipe: HANDLE, expected_session: u32) -> Result<(), String> {
    let mut server_session: u32 = 0;
    if GetNamedPipeServerSessionId(pipe, &mut server_session) == 0 {
        return Err(format!("GetNamedPipeServerSessionId failed: {}", GetLastError()));
    }
    if server_session != expected_session {
        return Err(format!("pipe server session mismatch: {} != {}", server_session, expected_session));
    }

    let mut owner_sid: PSID = std::ptr::null_mut();
    let mut sd: *mut c_void = std::ptr::null_mut();
    let sec_err = GetSecurityInfo(
        pipe, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION,
        &mut owner_sid, std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), &mut sd,
    );
    if sec_err != 0 {
        return Err(format!("GetSecurityInfo failed: {}", sec_err));
    }

    let res = (|| {
        let mut token: HANDLE = std::ptr::null_mut();
        if OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &mut token) == 0 {
            return Err(format!("OpenProcessToken failed: {}", GetLastError()));
        }
        let mut needed: u32 = 0;
        GetTokenInformation(token, TOKEN_USER, std::ptr::null_mut(), 0, &mut needed);
        let mut tok_buf = vec![0u8; needed as usize];
        let ok = GetTokenInformation(token, TOKEN_USER, tok_buf.as_mut_ptr(), needed, &mut needed);
        CloseHandle(token);
        if ok == 0 {
            return Err(format!("GetTokenInformation failed: {}", GetLastError()));
        }
        let user_sid = (*(tok_buf.as_ptr() as *const TokenUser)).user_sid;

        let mut admin_sid = [0u8; 68];
        let mut admin_len = admin_sid.len() as u32;
        if CreateWellKnownSid(WIN_BUILTIN_ADMINS_SID, std::ptr::null_mut(), admin_sid.as_mut_ptr(), &mut admin_len) == 0 {
            return Err(format!("CreateWellKnownSid(Admins) failed: {}", GetLastError()));
        }
        let mut sys_sid = [0u8; 68];
        let mut sys_len = sys_sid.len() as u32;
        if CreateWellKnownSid(WIN_LOCAL_SYSTEM_SID, std::ptr::null_mut(), sys_sid.as_mut_ptr(), &mut sys_len) == 0 {
            return Err(format!("CreateWellKnownSid(System) failed: {}", GetLastError()));
        }

        if EqualSid(owner_sid, user_sid) == 0
            && EqualSid(owner_sid, admin_sid.as_mut_ptr() as PSID) == 0
            && EqualSid(owner_sid, sys_sid.as_mut_ptr() as PSID) == 0
        {
            return Err("untrusted pipe server owner SID".to_string());
        }
        Ok(())
    })();

    if !sd.is_null() {
        LocalFree(sd);
    }
    res
}

unsafe fn send_rpc(pipe: HANDLE, json_line: &str) -> Result<String, String> {
    let mut frame = json_line.as_bytes().to_vec();
    frame.push(b'\n');
    let mut written: u32 = 0;
    if WriteFile(pipe, frame.as_ptr(), frame.len() as u32, &mut written, std::ptr::null_mut()) == 0
        || written as usize != frame.len()
    {
        return Err(format!("WriteFile failed: {}", GetLastError()));
    }

    let mut accum = Vec::new();
    let mut buf = vec![0u8; MAX_FRAME_BYTES];
    loop {
        let mut read: u32 = 0;
        let ok = ReadFile(pipe, buf.as_mut_ptr(), buf.len() as u32, &mut read, std::ptr::null_mut());
        if read > 0 {
            accum.extend_from_slice(&buf[..read as usize]);
        }
        if ok != 0 {
            break;
        }
        let err = GetLastError();
        if err == ERROR_MORE_DATA {
            if accum.len() > MAX_FRAME_BYTES {
                return Err("frame exceeds 64 KiB limit".to_string());
            }
            continue;
        }
        return Err(format!("ReadFile failed: {}", err));
    }
    Ok(String::from_utf8_lossy(&accum).trim().to_string())
}

fn main() {
    unsafe {
        let mut session_id: u32 = 0;
        if ProcessIdToSessionId(GetCurrentProcessId(), &mut session_id) == 0 {
            eprintln!("ProcessIdToSessionId failed: {}", GetLastError());
            return;
        }
        let pipe_name = to_wide_null(&format!("{}{}", PIPE_PREFIX, session_id));
        let pipe = CreateFileW(
            pipe_name.as_ptr(),
            DESIRED_ACCESS, 0, std::ptr::null(),
            OPEN_EXISTING, SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, std::ptr::null_mut(),
        );
        if pipe == INVALID_HANDLE_VALUE || pipe.is_null() {
            eprintln!("CreateFileW failed: {} (is Seer running?)", GetLastError());
            return;
        }

        let run_res = (|| -> Result<(), String> {
            verify_pipe_server(pipe, session_id)?;
            let mut mode = PIPE_READMODE_MESSAGE;
            if SetNamedPipeHandleState(pipe, &mut mode, std::ptr::null_mut(), std::ptr::null_mut()) == 0 {
                return Err(format!("SetNamedPipeHandleState failed: {}", GetLastError()));
            }
            let info = send_rpc(pipe, r#"{"jsonrpc":"2.0","id":1,"method":"seer.getInfo","params":{}}"#)?;
            println!("seer.getInfo: {}", info);

            let args: Vec<String> = env::args().collect();
            if args.len() > 1 {
                let abs_path = std::fs::canonicalize(Path::new(&args[1]))
                    .map(|p| p.to_string_lossy().into_owned().replace(r"\\?\", ""))
                    .unwrap_or_else(|_| args[1].clone());
                let open_req = format!(
                    r#"{{"jsonrpc":"2.0","id":2,"method":"seer.preview.open","params":{{"path":"{}","mode":"main","ifAlreadyOpen":"noop"}}}}"#,
                    escape_json(&abs_path)
                );
                println!("seer.preview.open: {}", send_rpc(pipe, &open_req)?);
            } else {
                println!("seer.preview.list: {}", send_rpc(pipe, r#"{"jsonrpc":"2.0","id":2,"method":"seer.preview.list","params":{}}"#)?);
                println!("Usage: seer_ipc.exe <file_path_to_preview>");
            }
            Ok(())
        })();

        CloseHandle(pipe);
        if let Err(e) = run_res {
            eprintln!("Error: {}", e);
        }
    }
}

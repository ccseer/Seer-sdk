//! Seer IPC Client for Rust
//! Uses raw Windows FFI to send WM_COPYDATA messages without heavy dependencies.

use std::env;
use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::Path;

const SEER_CLASS_NAME: &str = "SeerWindowClass";
const WM_COPYDATA: u32 = 0x004A;

const SEER_INVOKE_W32: usize = 5000;
const SEER_INVOKE_W32_SEP: usize = 5001;
const SEER_IS_VISIBLE: usize = 5004;
const SEER_HIDE: usize = 5005;
const SEER_GET_VERSION: usize = 5007;
const SEER_IS_TRACKING: usize = 5008;

type HWND = *mut std::ffi::c_void;
type LPARAM = isize;
type WPARAM = usize;
type LRESULT = isize;

#[repr(C)]
struct COPYDATASTRUCT {
    dw_data: usize,
    cb_data: u32,
    lp_data: *const std::ffi::c_void,
}

#[link(name = "user32")]
extern "system" {
    fn FindWindowW(lpClassName: *const u16, lpWindowName: *const u16) -> HWND;
    fn SendMessageW(hWnd: HWND, Msg: u32, wParam: WPARAM, lParam: *const COPYDATASTRUCT) -> LRESULT;
}

fn to_wide_null(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(Some(0)).collect()
}

pub struct SeerClient;

impl SeerClient {
    pub fn find_window() -> HWND {
        let class_name = to_wide_null(SEER_CLASS_NAME);
        unsafe { FindWindowW(class_name.as_ptr(), std::ptr::null()) }
    }

    pub fn is_running() -> bool {
        !Self::find_window().is_null()
    }

    pub fn send_simple_command(command: usize) -> isize {
        let hwnd = Self::find_window();
        if hwnd.is_null() {
            return 0;
        }

        let cds = COPYDATASTRUCT {
            dw_data: command,
            cb_data: 0,
            lp_data: std::ptr::null(),
        };

        unsafe { SendMessageW(hwnd, WM_COPYDATA, 0, &cds) }
    }

    pub fn send_path_command(command: usize, path: &str) -> bool {
        let hwnd = Self::find_window();
        if hwnd.is_null() || path.is_empty() {
            return false;
        }

        let abs_path = match std::fs::canonicalize(Path::new(path)) {
            Ok(p) => p.to_string_lossy().into_owned().replace(r"\\?\", ""),
            Err(_) => path.to_string(),
        };

        let wide_str = to_wide_null(&abs_path);
        let cds = COPYDATASTRUCT {
            dw_data: command,
            cb_data: (wide_str.len() * 2) as u32,
            lp_data: wide_str.as_ptr() as *const std::ffi::c_void,
        };

        unsafe { SendMessageW(hwnd, WM_COPYDATA, 0, &cds) != 0 }
    }

    pub fn preview(path: &str) -> bool {
        Self::send_path_command(SEER_INVOKE_W32, path)
    }

    pub fn preview_separated(path: &str) -> bool {
        Self::send_path_command(SEER_INVOKE_W32_SEP, path)
    }

    pub fn hide() {
        Self::send_simple_command(SEER_HIDE);
    }

    pub fn is_visible() -> bool {
        Self::send_simple_command(SEER_IS_VISIBLE) != 0
    }

    pub fn get_version() -> i32 {
        Self::send_simple_command(SEER_GET_VERSION) as i32
    }

    pub fn is_tracking_enabled() -> bool {
        Self::send_simple_command(SEER_IS_TRACKING) != 0
    }
}

fn main() {
    println!("=== Seer Rust IPC Demo ===");

    if !SeerClient::is_running() {
        println!("Seer is not running! Please start Seer first.");
        return;
    }

    println!("Seer Version: {}", SeerClient::get_version());
    println!("Is Visible: {}", SeerClient::is_visible());
    println!("Is Tracking: {}", SeerClient::is_tracking_enabled());

    let args: Vec<String> = env::args().collect();
    if args.len() > 1 {
        let path = &args[1];
        println!("Invoking preview for: {}", path);
        SeerClient::preview(path);
    } else {
        println!("Usage: seer_ipc.exe <file_path_to_preview>");
    }
}

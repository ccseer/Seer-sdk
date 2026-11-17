#pragma once

#define SEER_CLASS_NAME L"SeerWindowClass"
#define SEER_VIEWER_CLASS_NAME L"SeerViewerWindowClass"

enum SeerCommand : int {
    // Request the file path (sent from Seer)
    SEER_REQUEST_PATH = 4000,
    // Respond with the file path (send back to Seer)
    SEER_RESPONSE_PATH = 4001,
    // Dynamic explorer registration and unregistration (V2)
    SEER_REGISTER_EXPLORER   = 4002,
    SEER_UNREGISTER_EXPLORER = 4003,

    // Request a preview：
    // Note: The minimum interval between SEER_INVOKE_* commands should be 200ms
    SEER_INVOKE_W32 = 5000,
    // Preview in a separate window
    SEER_INVOKE_W32_SEP = 5001,

    // Seer can have multiple windows(main + separated) at the same time
    // Check if the main window is currently visible
    SEER_IS_VISIBLE = 5004,
    // Hide the main window if it is currently visible
    SEER_HIDE = 5005,

    // Request the currently previewed file
    SEER_GET_CURRENT_FILE = 5006,
    // Request the current Seer version
    // 4.0.1 => 40001
    SEER_GET_VERSION = 5007,
    // Query whether tracking mode is enabled
    SEER_IS_TRACKING_ENABLED = 5008,

    // Request a preview in a standalone viewer process (SeerViewer.exe)
    SEER_INVOKE_W32_STANDALONE = 5009,
    // Request a preview and pin the window (always on top)
    SEER_INVOKE_W32_PIN = 5010,

    // V2 JSON-RPC unified request / response payload (UTF-8 JSON via WM_COPYDATA)
    SEER_V2_REQUEST  = 6000,
    SEER_V2_RESPONSE = 6001,
};

// Seer startup ready broadcast message name (RegisterWindowMessageW)
#define SEER_CORE_READY_MSG L"SEER_CORE_READY"

// Named Pipe name prefix; full name is prefix + decimal current session ID
#define SEER_PIPE_NAME_PREFIX L"\\\\.\\pipe\\Seer.IPC.v2."

// SEER_IS_VISIBLE
enum SeerVisibility : unsigned char { VISIBLE_FALSE = 0, VISIBLE_TRUE = 1 };

//////////////////////////////////////////
// Legacy only: used by older Seer versions; newer Seer versions no longer read explorer JSON files.
// json file
// file should be located at  %USERPROFILE%/Documents/Seer/explorers/
#define SEER_EXPLORER_FOLDER "explorers"
// required key
#define SEER_JSON_KEY_CLASSNAME "classname"
// optional
#define SEER_JSON_KEY_WINDOWTEXT "windowtext"
#define SEER_JSON_KEY_APPNAME "appname"

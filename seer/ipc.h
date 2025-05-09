#pragma once

#define SEER_CLASS_NAME L"SeerWindowClass"

enum SeerCommand : int {
    // Request the file path (sent from Seer)
    SEER_REQUEST_PATH = 4000,
    // Respond with the file path (send back to Seer)
    SEER_RESPONSE_PATH = 4001,

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
};

// SEER_IS_VISIBLE
enum SeerVisibility : unsigned char { VISIBLE_FALSE = 0, VISIBLE_TRUE = 1 };

//////////////////////////////////////////
// json file
// file should be located at  %USERPROFILE%/Documents/Seer/explorers/
#define SEER_EXPLORER_FOLDER "explorers"
// required key
#define SEER_JSON_KEY_CLASSNAME "classname"
// optional
#define SEER_JSON_KEY_WINDOWTEXT "windowtext"
#define SEER_JSON_KEY_APPNAME "appname"

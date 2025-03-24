#pragma once

// send to seer
#define SEER_OIT_MSG_W32 6500
#define SEER_OIT_SUB_W32_LOAD_ERR 6501
#define SEER_OIT_SUB_W32_LOAD_OK 6502
#define SEER_OIT_SUB_W32_KEY_PRESS 6503
#define SEER_OIT_SUB_W32_WAGGLE 6504
#define SEER_OIT_SUB_W32_TOAST 6505

// exe return
#define ERR_BAD_ARG (-1)
#define ERR_FILE_NOT_FOUND (-2)
#define ERR_PROCESS (-3)

#define SEER_CLASSNAME "SeerWindowClass"

// json key for msg data
#define SERR_MSG_KEY_WID "window_index"
#define SERR_MSG_KEY_SUB_ID "msg_sub_id"
#define SERR_MSG_KEY_TOAST "toast_text"
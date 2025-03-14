#pragma once

#include <QDataStream>

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

//////////////////////////
/// bad idea
#define SEER_OIT_MSG 6000
#define SEER_OIT_SUB_LOAD_ERR 6001
#define SEER_OIT_SUB_LOAD_OK 6002
#define SEER_OIT_SUB_KEY_PRESS 6003
#define SEER_OIT_SUB_WAGGLE 6004
#define SEER_OIT_SUB_TOAST 6005
// msg from seer
// first msg from seer
// then the following three msg will be sent immediately
#define SEER_OIT_ATTACHED 6102
// will be messaged anytime it changed
#define SEER_OIT_SIZE_CHANGED 6101
// will be messaged anytime it changed
#define SEER_OIT_DPI_CHANGED 6100
// only once, no preview window when changing theme setttings
#define SEER_OIT_THEME_CHANGED 6103

// TODO: OITData
// struct MyData {
//     int sub_type;
//     int wnd_index;
//    size_t data_size; 
//    BYTE* data;        
// };
struct OITData {
    // sub_type -> SEER_OIT_SUB_*
    int sub_type  = 0;
    int wnd_index = 0;
    QByteArray data;
};

inline QDataStream& operator<<(QDataStream& out, const OITData& v)
{
    out << v.sub_type << v.wnd_index << v.data;
    return out;
}

inline QDataStream& operator>>(QDataStream& in, OITData& v)
{
    in >> v.sub_type;
    in >> v.wnd_index;
    in >> v.data;
    return in;
}

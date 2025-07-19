#pragma once

#include <qt_windows.h>

#include <QDir>
#include <QString>

namespace seer {
inline QString getDLLPath()
{
    MEMORY_BASIC_INFORMATION mbi = {};
    VirtualQuery((void *)getDLLPath, &mbi, sizeof(mbi));
    HMODULE hm = (HMODULE)mbi.AllocationBase;

    QString dir;
    TCHAR path[MAX_PATH] = {};
    if (hm && GetModuleFileName(hm, path, MAX_PATH)) {
        dir = QString::fromWCharArray(path);
        dir = QFileInfo(dir).absoluteDir().absolutePath();
    }
    else {
        qDebug() << "GetModuleFileName" << GetLastError();
    }
    return dir;
}
}  // namespace seer

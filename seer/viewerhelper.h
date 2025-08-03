#pragma once

#include <qt_windows.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QSize>

namespace seer {
inline QString getDLLPath()
{
    MEMORY_BASIC_INFORMATION mbi = {};
    VirtualQuery((void*)getDLLPath, &mbi, sizeof(mbi));
    HMODULE hm = (HMODULE)mbi.AllocationBase;

    QString dir;
    TCHAR path[MAX_PATH] = {};
    if (hm && GetModuleFileName(hm, path, MAX_PATH)) {
        dir = QString::fromWCharArray(path);
        dir = QFileInfo(dir).absoluteDir().absolutePath();
    }
    else {
        qDebug() << Q_FUNC_INFO << GetLastError();
    }
    return dir;
}

inline QSize parseViewerSizeFromConfig(const QStringList& args_raw)
{
    if (args_raw.isEmpty()) {
        return {};
    }
    // arguments must start with the name of the executable
    QStringList args{"dummy"};
    args.append(args_raw);

    QCommandLineParser parser;
    QCommandLineOption opt_p{
        QStringList() << "p" << "percent",
        "Viewer window size as a percentage of the primary screen (1-100)",
        "percent"};
    QCommandLineOption opt_w{QStringList() << "w" << "width", "Viewer width",
                             "width"};
    QCommandLineOption opt_h{QStringList() << "h" << "height", "Viewer height",
                             "height"};
    parser.addOptions({opt_p, opt_w, opt_h});

    if (!parser.parse(args)) {
        qDebug() << parser.errorText();
    }
    bool cvt_p = false;
    int pct    = parser.value(opt_p).toInt(&cvt_p);
    if (cvt_p && pct > 0 && pct <= 100) {
        if (QScreen* scn = QGuiApplication::primaryScreen()) {
            return scn->availableSize() * pct / 100;
        }
    }
    bool cvt_w = false, cvt_h = false;
    const int width  = parser.value(opt_w).toInt(&cvt_w);
    const int height = parser.value(opt_h).toInt(&cvt_h);
    if (cvt_w && cvt_h) {
        return {width, height};
    }
    return {};
}
}  // namespace seer

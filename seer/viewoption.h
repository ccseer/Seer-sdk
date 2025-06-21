#pragma once

#include <QSize>
#include <QString>

struct ViewOptionsData {
    ViewOptionsData() : theme(-1), dpr(0.1), is_main_wnd(true), wnd_index(0) {}

    // viewer name
    QString type;
    // absolute file path
    QString path;
    // lower case.
    // When the file has no suffix, the suffix might be detected rather than
    // taken from the path.
    QString suffix;

    QSize size_max_viewer;
    QSize size_min_viewer;

    // 1: dark, 0: light
    int theme;
    // scale factor
    qreal dpr;

    // main wnd has a different title bar
    bool is_main_wnd;
    // wnd_index == 0 => main wnd
    ushort wnd_index;
};

class ViewOptions {
public:
    ViewOptions() : d(std::make_unique<ViewOptionsData>()) {}
    virtual ~ViewOptions() {}

    std::unique_ptr<ViewOptionsData> d;
};
using ViewOptionsPtr = std::unique_ptr<ViewOptions>;

enum ViewCommandType : int {
    VCT_StateChange = 0,        // ViewCommandValue_StateChange
    VCT_OpenByDefaultSoftware,  // const QString& path
    VCT_RevealInExplorer,       // const QString& path
    VCT_AppendProperty,         // const QVector<QPair<QString, QString>>&

    VCT_WiggleWindow,
    VCT_ResizeWindow,
    VCT_HideWindow,

    VCT_ShowToastMsg,  // const QString& msg
    VCT_HideToastMsg,

    // the file property remains the same
    VCT_LoadViewerWithNewPath,  // const QString& path
    // the file property changes
    VCT_LoadWindowWithNewPath = 10,  // const QString& path

    VCT_LoadViewerWithNewType,  // const QString& viewer_name

    VCT_LoadSeparated,  // const QString& path

    // --------------------
    VCT_Reserved_Max = 999
};

enum ViewCommandValue_StateChange : uchar {
    // VCV_None = 0,
    VCV_Loading,
    VCV_Loaded,
    VCV_Error
};

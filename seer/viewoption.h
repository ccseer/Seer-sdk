#pragma once

#include <QVariant>

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

// Forward declaration
class ViewOptionsPrivate;

class ViewOptions {
public:
    ViewOptions() = default;

    // Core field accessors (typed, high-performance)
    QString path() const;
    void setPath(const QString &path);

    QString suffix() const;
    void setSuffix(const QString &suffix);

    QString viewerType() const;
    void setViewerType(const QString &type);

    int theme() const;
    void setTheme(int theme);

    qreal dpr() const;
    void setDpr(qreal dpr);

    // Extension field accessors (Property Bag)
    QVariant property(const QString &key,
                      const QVariant &defaultValue = {}) const;
    void setProperty(const QString &key, const QVariant &value);
    bool hasProperty(const QString &key) const;
    void removeProperty(const QString &key);
    QStringList propertyKeys() const;

    ViewOptionsPrivate *d_ptr = nullptr;  // Raw pointer, 8 bytes
};

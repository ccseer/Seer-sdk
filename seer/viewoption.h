#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantHash>

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

// Private implementation (inline, header-only)
class ViewOptionsPrivate {
public:
    ViewOptionsPrivate() = default;

    ViewOptionsPrivate(const ViewOptionsPrivate &other)
        : path(other.path),
          suffix(other.suffix),
          viewer_type(other.viewer_type),
          theme(other.theme),
          dpr(other.dpr),
          extras(other.extras)
    {
    }

    QString path;
    QString suffix;
    QString viewer_type;
    int theme = -1;
    qreal dpr = 1.0;
    QVariantHash extras;
};

class ViewOptions {
public:
    ViewOptions() = default;

    // Core field accessors
    QString path() const
    {
        return d_ptr->path;
    }
    void setPath(const QString &path)
    {
        d_ptr->path = path;
    }

    QString suffix() const
    {
        return d_ptr->suffix;
    }
    void setSuffix(const QString &suffix)
    {
        d_ptr->suffix = suffix;
    }

    QString viewerType() const
    {
        return d_ptr->viewer_type;
    }
    void setViewerType(const QString &type)
    {
        d_ptr->viewer_type = type;
    }

    int theme() const
    {
        return d_ptr->theme;
    }
    void setTheme(int theme)
    {
        d_ptr->theme = theme;
    }

    qreal dpr() const
    {
        return d_ptr->dpr;
    }
    void setDpr(qreal dpr)
    {
        d_ptr->dpr = dpr;
    }

    // Extension field accessors (Property Bag)
    QVariant property(const QString &key,
                      const QVariant &defaultValue = {}) const
    {
        return d_ptr->extras.value(key, defaultValue);
    }

    void setProperty(const QString &key, const QVariant &value)
    {
        d_ptr->extras[key] = value;
    }

    bool hasProperty(const QString &key) const
    {
        return d_ptr->extras.contains(key);
    }

    void removeProperty(const QString &key)
    {
        d_ptr->extras.remove(key);
    }

    QStringList propertyKeys() const
    {
        return d_ptr->extras.keys();
    }

    ViewOptionsPrivate *d_ptr = nullptr;
};

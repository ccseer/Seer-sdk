#pragma once
#define SEER_VIEWOPTION_H

#include <QSize>
#include <QString>
#include <QStringList>
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

// g_property_key_cmd is used by DLL plugins to pass command-line args
inline constexpr auto g_property_key_cmd = "plugin_cmd";

// Extension field keys (public - for PathResolver, plugins)
inline constexpr const char* VO_KEY_ARCHIVE_PATH  = "archive_path";
inline constexpr const char* VO_KEY_INNER_PATH    = "inner_path";
inline constexpr const char* VO_KEY_RESOLVED_PATH = "resolved_path";

// Forward declaration
class ViewOptionsPrivate;

/**
 * ViewOptions v3 - ABI-stable viewer configuration
 *
 * Lifecycle:
 * - Created by Seer via create() factory
 * - Borrowed by plugins (const ViewOptions*)
 * - Destroyed by Seer via destroy()
 *
 * ABI Guarantee:
 * - sizeof(ViewOptions) = 8 bytes (one pointer)
 * - All methods are non-inline (in .cpp)
 * - Future fields added to ViewOptionsPrivate only
 *
 * Compiler Requirements:
 * - MSVC: /MD (Multi-threaded DLL) runtime
 * - DO NOT USE: /MT or /MTd (static linking)
 */
class ViewOptions {
public:
    // Static factory methods (force Seer CRT allocation)
    static ViewOptions* create();
    static ViewOptions* createFrom(const ViewOptions* parent);

    // Destruction (Seer CRT deallocation)
    void destroy();

    // Core field accessors (typed, high-performance)
    QString path() const;
    void setPath(const QString& path);

    QString suffix() const;
    void setSuffix(const QString& suffix);

    QString viewerType() const;
    void setViewerType(const QString& type);

    int theme() const;
    void setTheme(int theme);

    qreal dpr() const;
    void setDpr(qreal dpr);

    QSize maxSize() const;
    void setMaxSize(const QSize& size);

    QSize minSize() const;
    void setMinSize(const QSize& size);

    // Extension field accessors (Property Bag)
    QVariant property(const QString& key,
                      const QVariant& defaultValue = {}) const;
    void setProperty(const QString& key, const QVariant& value);
    bool hasProperty(const QString& key) const;
    void removeProperty(const QString& key);
    QStringList propertyKeys() const;

private:
    ViewOptions();   // Private - force factory usage
    ~ViewOptions();  // Private - force destroy() usage

    ViewOptions(const ViewOptions&)            = delete;
    ViewOptions& operator=(const ViewOptions&) = delete;

    ViewOptionsPrivate* d_ptr;  // Raw pointer, 8 bytes
};

# Seer SDK

Official SDK for developing Seer file preview plugins.

## Version

Current version: **v3.0** (2026-04-20)

## Requirements

- **Qt**: 5.15+ or Qt 6.x
- **C++ Standard**: C++17 or higher
- **Compiler**: MSVC 2019+ (Windows)
- **Build System**: CMake 3.22+ or qmake

## Critical: Compiler Settings

### Runtime Library (REQUIRED)

Your plugin **MUST** use the same runtime library as Seer:

- **Debug**: `/MDd` (Multi-threaded Debug DLL)
- **Release**: `/MD` (Multi-threaded DLL)

**DO NOT USE**: `/MT` or `/MTd` (static linking)

### Why This Matters

Plugins share memory with Seer.exe across DLL boundaries. Using different runtime libraries causes:
- Memory allocation/deallocation crashes
- Heap corruption
- Undefined behavior

## SDK v3 Overview

SDK v3 provides **ABI-stable** interfaces for plugin development:

- **ViewOptions v3**: Configuration passed to plugins (read-only, borrowed)
- **ViewerBase v3**: Base class for custom viewers
- **ViewerPluginInterface v3**: Plugin interface (IID: `"seer.plugin.interface.preview/3"`)

### Key Features

✅ **ABI Stability**: Binary compatibility across Seer versions  
✅ **Performance**: Typed core fields, no hash lookup overhead  
✅ **Extensibility**: Property Bag for custom fields  
✅ **Safety**: Factory methods prevent cross-DLL memory issues  

## Quick Start

### 1. Include SDK Headers

```cpp
#include "seer/viewerbase.h"
#include "seer/viewoption.h"
```

### 2. Implement Your Viewer

```cpp
class MyViewer : public ViewerBase {
    Q_OBJECT
public:
    explicit MyViewer(QWidget* parent = nullptr);
    
    QString name() const override { return "MyViewer"; }
    QSize getContentSize() const override;
    
    // REQUIRED: Safe cross-DLL destruction
    void destroy() override { delete this; }
    
protected:
    void loadImpl(QBoxLayout* layout_content,
                  QHBoxLayout* layout_control_bar) override;
};

void MyViewer::loadImpl(QBoxLayout* layout_content,
                        QHBoxLayout* layout_control_bar)
{
    // Access ViewOptions (borrowed, read-only)
    QString path = options()->path();
    int theme = options()->theme();
    qreal dpr = options()->dpr();
    
    // Create your UI
    auto label = new QLabel(path);
    layout_content->addWidget(label);
    
    // Signal when loaded
    emit sigCommand(VCT_StateChange, VCV_Loaded);
}
```

### 3. Implement Plugin Interface

```cpp
class MyPlugin : public QObject, public ViewerPluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ViewerPluginInterface_iid)
    Q_INTERFACES(ViewerPluginInterface)
    
public:
    ViewerBase* createViewer(QWidget* parent = nullptr) override {
        return new MyViewer(parent);
    }
};
```

### 4. Build Your Plugin

**CMake**:
```cmake
add_library(my_plugin SHARED
    my_viewer.cpp
    my_plugin.cpp
)

target_link_libraries(my_plugin
    Qt6::Core
    Qt6::Widgets
)

# CRITICAL: Set runtime library
set_target_properties(my_plugin PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
```

**qmake**:
```pro
TEMPLATE = lib
CONFIG += plugin
QT += core widgets

HEADERS += my_viewer.h
SOURCES += my_viewer.cpp my_plugin.cpp

# CRITICAL: Set runtime library
QMAKE_CXXFLAGS_RELEASE += /MD
QMAKE_CXXFLAGS_DEBUG += /MDd
```

## ViewOptions v3 API

### Core Fields (Typed, High-Performance)

```cpp
QString path() const;              // File path
QString suffix() const;            // File extension (lowercase)
QString viewerType() const;        // Viewer type name
int theme() const;                 // 0=light, 1=dark
qreal dpr() const;                 // Device pixel ratio
QSize maxSize() const;             // Maximum viewer size
QSize minSize() const;             // Minimum viewer size
```

### Extension Fields (Property Bag)

```cpp
QVariant property(const QString& key, 
                  const QVariant& defaultValue = {}) const;
bool hasProperty(const QString& key) const;
QStringList propertyKeys() const;
```

**Public Extension Keys**:
- `VO_KEY_ARCHIVE_PATH` - Archive file path (for nested files)
- `VO_KEY_INNER_PATH` - Path inside archive
- `VO_KEY_RESOLVED_PATH` - Resolved file path

### Lifecycle

ViewOptions is **borrowed** (not owned) by plugins:

```cpp
void loadImpl(QBoxLayout* layout_content,
              QHBoxLayout* layout_control_bar) override
{
    // options() returns const ViewOptions*
    // Valid for the entire lifetime of the viewer
    // Safe to call anytime after load() is called
    QString path = options()->path();
    int theme = options()->theme();
}
```

**Important**: The `ViewOptions*` returned by `options()` remains valid for the entire lifetime of your viewer. You can safely call `options()` in any method after `load()` has been called. However, you should still prefer copying values you need frequently rather than repeatedly calling `options()` for performance reasons.

## Migration from v2

### Interface Changes

**v2**:
```cpp
void load(QHBoxLayout* layout, std::unique_ptr<ViewOptions>&& ctx);
```

**v3**:
```cpp
void load(QHBoxLayout* layout, const ViewOptions* ctx);
```

### Field Access Changes

**v2**:
```cpp
QString path = m_d->d->path;
int theme = m_d->d->theme;
```

**v3**:
```cpp
QString path = options()->path();
int theme = options()->theme();
```

### Plugin Interface Changes

**v2**:
```cpp
#define ViewerPluginInterface_iid "seer.plugin.interface.preview/2"
```

**v3**:
```cpp
#define ViewerPluginInterface_iid "seer.plugin.interface.preview/3"
```

### Migration Steps

1. Update `#include "seer/viewerbase.h"` (no version suffix)
2. Update `#include "seer/viewoption.h"` (no version suffix)
3. Change IID to `ViewerPluginInterface_iid` (v3)
4. Replace `m_d->d->field` with `options()->field()`
5. Update `load()` signature to use `const ViewOptions*`
6. **Add `void destroy() override { delete this; }` to your viewer class**
7. Rebuild with `/MD` runtime

## Backward Compatibility

Seer v4.0+ supports multiple plugin versions:

- **v3 plugins**: Native support (recommended) - Factory interface with ABI-stable ViewOptions
- **v2 plugins**: Automatic adapter (compatible, requires `/MD` runtime) - Factory interface with move-semantics ViewOptions

Old v2 plugins continue to work without recompilation if they were built with `/MD` runtime.

**Note**: v1 plugins (pre-factory interface) are no longer supported. Please migrate to v3 SDK.

## Best Practices

### 1. Cache Frequently Used Values

```cpp
// ✅ GOOD - Cache values for performance
class MyViewer : public ViewerBase {
    QString m_path;
    int m_theme;
    
    void loadImpl(...) {
        // Cache frequently accessed values
        m_path = options()->path();
        m_theme = options()->theme();
    }
    
    void someMethod() {
        // Use cached values for better performance
        if (m_theme == 1) { /* dark mode */ }
    }
};

// ⚠️ ACCEPTABLE - Call options() directly if not performance-critical
class MyViewer : public ViewerBase {
    void someMethod() {
        // options() is always valid, but repeated calls have overhead
        QString path = options()->path();
    }
};
```

### 2. Use Typed Fields for Performance

```cpp
// ✅ GOOD - Direct access, no hash lookup
QString path = options()->path();
int theme = options()->theme();

// ⚠️ SLOWER - Hash lookup + QVariant unboxing
QString path = options()->property("path").toString();
```

### 3. Check Extension Fields

```cpp
// ✅ GOOD - Check before access
if (options()->hasProperty(VO_KEY_ARCHIVE_PATH)) {
    QString archive = options()->property(VO_KEY_ARCHIVE_PATH).toString();
}
```

### 4. Signal State Changes

```cpp
void loadImpl(...) {
    // Signal loading started (automatic)
    
    // ... load content ...
    
    // Signal loading completed
    emit sigCommand(VCT_StateChange, VCV_Loaded);
}
```

## Example Plugins

See `examples/` directory for complete plugin templates:

- `simple_viewer/` - Minimal text viewer
- `advanced_viewer/` - Full-featured image viewer with controls

## Troubleshooting

### Crash on Plugin Load

**Symptom**: Seer crashes when loading your plugin

**Cause**: Runtime library mismatch

**Solution**: Verify `/MD` (Release) or `/MDd` (Debug) in compiler settings

### "Failed to cast to ViewerPluginInterface"

**Symptom**: Plugin not recognized

**Cause**: Wrong IID or missing Q_INTERFACES

**Solution**: Use `ViewerPluginInterface_iid` and declare `Q_INTERFACES(ViewerPluginInterface)`

### Access Violation in ViewOptions

**Symptom**: Crash when accessing `options()->path()`

**Cause**: Calling `options()` before `load()` has been called, or after the viewer has been destroyed

**Solution**: Only call `options()` after `load()` and before `destroy()`

## License

See main Seer repository for license information.

## Support

- **Issues**: https://github.com/ccseer/Seer/issues
- **SDK Repository**: https://github.com/ccseer/Seer-sdk
- **Documentation**: https://github.com/ccseer/Seer-sdk/wiki

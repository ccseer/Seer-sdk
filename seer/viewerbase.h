#pragma once

#include <QBoxLayout>
#include <QWidget>

#include "viewoption.h"

class ViewerBasePrivate;

/**
 * ViewerBase v3 - ABI-stable base class for viewers
 *
 * Key changes from v2:
 * - Borrowing semantics: const ViewOptions* (not unique_ptr&&)
 * - D-Pointer for ABI stability
 * - destroy() method for safe cross-DLL cleanup
 *
 * Lifecycle:
 * - Created by plugin via createViewer()
 * - Loaded by Seer with borrowed ViewOptions*
 * - Destroyed by Seer via destroy()
 */
class ViewerBase : public QWidget {
    Q_OBJECT
public:
    explicit ViewerBase(QWidget* parent = nullptr);

    // Borrowing load interface
    void load(QHBoxLayout* layout_control_bar, const ViewOptions* ctx);

    virtual QString name() const         = 0;
    virtual QSize getContentSize() const = 0;

    virtual void loadFileInfo() {}
    virtual void onCopyTriggered() {}
    virtual void updateDPR(qreal /*dpr*/) {}
    virtual void updateTheme(int /*theme*/) {}

    // Safe destruction across DLL boundary
    virtual void destroy() = 0;

    Q_SIGNAL void sigCommand(int view_command_type, const QVariant& data = {});

protected:
    virtual ~ViewerBase();  // Protected - use destroy()

    virtual void loadImpl(QBoxLayout* layout_content,
                          QHBoxLayout* layout_control_bar) = 0;

    // Access borrowed ViewOptions
    const ViewOptions* options() const;

private:
    ViewerBasePrivate* d_ptr;  // Raw pointer for ABI stability
};

// Plugin interface v3
class ViewerPluginInterface {
public:
    virtual ~ViewerPluginInterface()                            = default;
    virtual ViewerBase* createViewer(QWidget* parent = nullptr) = 0;
};

#define ViewerPluginInterface_iid "seer.plugin.interface.preview/3"
Q_DECLARE_INTERFACE(ViewerPluginInterface, ViewerPluginInterface_iid)

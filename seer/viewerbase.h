#pragma once

#include <QBoxLayout>
#include <QWidget>

#include "viewoption.h"

class ViewerBasePrivate;

class ViewerBase : public QWidget {
    Q_OBJECT
public:
    explicit ViewerBase(QWidget *parent = nullptr);

    // Borrowing load interface
    void load(QHBoxLayout *layout_control_bar, const ViewOptions *ctx);

    virtual QString name() const = 0;
    // preferred size
    // if it's image type, its size = 80000x80000, then return the val
    virtual QSize getContentSize() const = 0;

    virtual void loadFileInfo() {}
    virtual void onCopyTriggered() {}
    virtual void updateDPR(qreal /*dpr*/) {}
    virtual void updateTheme(int /*theme*/) {}

    Q_SIGNAL void sigCommand(int view_command_type, const QVariant &data = {});

protected:
    virtual ~ViewerBase();

    virtual void loadImpl(QBoxLayout *layout_content,
                          QHBoxLayout *layout_control_bar)
        = 0;

    // Access borrowed ViewOptions
    const ViewOptions *options() const;

private:
    ViewerBasePrivate *d_ptr;
};

//////////////////////////////////////////////////////////////////
class ViewerPluginInterface {
public:
    virtual ~ViewerPluginInterface()                            = default;
    virtual ViewerBase *createViewer(QWidget *parent = nullptr) = 0;
};

#define ViewerPluginInterface_iid "seer.plugin.interface.preview/3"
Q_DECLARE_INTERFACE(ViewerPluginInterface, ViewerPluginInterface_iid)

#pragma once

#include <QBoxLayout>
#include <QDebug>
#include <QVBoxLayout>
#include <QWidget>
#include <chrono>

#include "viewoption.h"

// Private implementation (inline, header-only)
class ViewerBasePrivate {
public:
    const ViewOptions *options = nullptr;
    std::chrono::high_resolution_clock::time_point elapsed;
    QVBoxLayout *layout = nullptr;

    ViewerBasePrivate() : layout(new QVBoxLayout()) {}
    ~ViewerBasePrivate()
    {
        delete layout;
    }
};

class ViewerBase : public QWidget {
    Q_OBJECT
public:
    explicit ViewerBase(QWidget *parent = nullptr)
        : QWidget(parent), d_ptr(new ViewerBasePrivate())
    {
        setLayout(d_ptr->layout);
        d_ptr->layout->setSpacing(0);
        d_ptr->layout->setContentsMargins(0, 0, 0, 0);

        connect(this, &ViewerBase::sigCommand, this,
                [this](auto t, const auto &v) {
                    if (t != VCT_StateChange || v.toInt() != VCV_Loaded) {
                        return;
                    }
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration
                        = std::chrono::duration_cast<std::chrono::milliseconds>(
                            end_time - d_ptr->elapsed);
                    qDebug() << "[wnd] [timer] elapsed:" << this
                             << duration.count() << "ms";
                });
    }

    // Borrowing load interface
    void load(QHBoxLayout *layout_control_bar, const ViewOptions *ctx)
    {
        emit sigCommand(VCT_StateChange, VCV_Loading);

        d_ptr->elapsed = std::chrono::high_resolution_clock::now();
        d_ptr->options = ctx;

        qDebug() << "[wnd] [timer] started:" << this << ctx->theme()
                 << ctx->dpr();

        loadImpl(d_ptr->layout, layout_control_bar);
    }

    virtual QString name() const         = 0;
    virtual QSize getContentSize() const = 0;

    virtual void loadFileInfo() {}
    virtual void onCopyTriggered() {}
    virtual void updateDPR(qreal /*dpr*/) {}
    virtual void updateTheme(int /*theme*/) {}

    Q_SIGNAL void sigCommand(int view_command_type, const QVariant &data = {});

protected:
    virtual ~ViewerBase()
    {
        delete d_ptr;
    }

    virtual void loadImpl(QBoxLayout *layout_content,
                          QHBoxLayout *layout_control_bar)
        = 0;

    // Access borrowed ViewOptions
    const ViewOptions *options() const
    {
        return d_ptr->options;
    }

private:
    ViewerBasePrivate *d_ptr;
};

class ViewerPluginInterface {
public:
    virtual ~ViewerPluginInterface()                            = default;
    virtual ViewerBase *createViewer(QWidget *parent = nullptr) = 0;
};

#define ViewerPluginInterface_iid "seer.plugin.interface.preview/3"
Q_DECLARE_INTERFACE(ViewerPluginInterface, ViewerPluginInterface_iid)

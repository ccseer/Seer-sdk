#pragma once

#include <QBoxLayout>
#include <QWidget>

#include "viewoption.h"

class ViewerBase : public QWidget {
    Q_OBJECT
public:
    explicit ViewerBase(QWidget* parent = nullptr);
    ~ViewerBase() override = default;

    void load(QHBoxLayout* layout_control_bar,
              std::unique_ptr<ViewOptions>&& ctx);

    virtual QString name() const = 0;
    // preferred size
    // if it's image type, its size = 80000x80000, then return the val
    virtual QSize getContentSize() const = 0;

    virtual void loadFileInfo() {}
    virtual void onCopyTriggered() {}

    virtual void updateDPR(qreal /*dpr*/) {}
    virtual void updateTheme(int /*theme*/) {}

    Q_SIGNAL void sigCommand(int view_command_type, const QVariant& data = {});

    QString getPath() const
    {
        return m_d ? m_d->d->path : QString();
    }

protected:
    virtual void loadImpl(QBoxLayout* layout_content,
                          QHBoxLayout* layout_control_bar)
        = 0;
    std::unique_ptr<ViewOptions> m_d;

private:
    class Impl {
    public:
        std::chrono::high_resolution_clock::time_point elapsed;
        QVBoxLayout* layout;

        Impl() : layout(new QVBoxLayout()) {}
        ~Impl()
        {
            delete layout;
        }
    };

    std::unique_ptr<Impl> m_impl;
};

inline ViewerBase::ViewerBase(QWidget* parent)
    : QWidget(parent), m_impl(std::make_unique<Impl>())
{
    this->setLayout(m_impl->layout);
    m_impl->layout->setSpacing(0);
    m_impl->layout->setContentsMargins(0, 0, 0, 0);
    connect(this, &ViewerBase::sigCommand, this, [this](auto t, const auto& v) {
        if (t != ViewCommandType::VCT_StateChange) {
            return;
        }
        if (v.toInt() != VCV_Loaded) {
            return;
        }
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - m_impl->elapsed);
        qDebug() << "[wnd] [timer] elapsed:" << this << duration.count()
                 << "ms";
    });
}

inline void ViewerBase::load(QHBoxLayout* layout_control_bar,
                             std::unique_ptr<ViewOptions>&& ctx)
{
    emit sigCommand(ViewCommandType::VCT_StateChange, VCV_Loading);

    m_impl->elapsed = std::chrono::high_resolution_clock::now();
    m_d             = std::move(ctx);
    qDebug() << "[wnd] [timer] started:" << this << m_d->d->is_main_wnd
             << m_d->d->dpr;

    loadImpl(m_impl->layout, layout_control_bar);
}

//////////////////////////////////////////////////////////////////
class ViewerFactoryInterface {
public:
    virtual ~ViewerFactoryInterface();
    virtual ViewerBase* createViewer(QWidget* parent = nullptr) = 0;
};

#define ViewerFactoryInterface_iid "seer.plugin.interface.preview/1.1"
Q_DECLARE_INTERFACE(ViewerFactoryInterface, ViewerFactoryInterface_iid)

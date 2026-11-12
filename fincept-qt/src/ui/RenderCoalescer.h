#pragma once
// RenderCoalescer.h — at most one render per event-loop pass.
//
// A price-stream tick batch publishes many market:quote:* topics in one go;
// a widget that re-renders inside every quote callback would rebuild N times
// for one batch. request() instead queues a single render after the current
// event finishes — every quote of the batch is in the cache by then.
//
// Lives as a member of the widget it renders; `ctx` (that widget) scopes the
// queued call, so a destroyed widget never renders.

#include <QObject>
#include <QTimer>

#include <functional>
#include <utility>

namespace fincept::ui {

class RenderCoalescer {
  public:
    RenderCoalescer(QObject* ctx, std::function<void()> render) : ctx_(ctx), render_(std::move(render)) {}
    RenderCoalescer(const RenderCoalescer&) = delete;
    RenderCoalescer& operator=(const RenderCoalescer&) = delete;

    void request() {
        if (pending_)
            return;
        pending_ = true;
        QTimer::singleShot(0, ctx_, [this]() {
            pending_ = false;
            render_();
        });
    }

  private:
    QObject* ctx_;
    std::function<void()> render_;
    bool pending_ = false;
};

} // namespace fincept::ui

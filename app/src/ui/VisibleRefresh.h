#pragma once
// VisibleRefresh.h — "keep this current while someone is looking at it".
//
// For views that load once and then sit on that answer for as long as they
// stay open: a calendar tile fetched in its constructor, an options chain
// fetched on tab activation. An app left open overnight showed yesterday's
// calendar and ranks with nothing saying so.
//
// Two modes, chosen by what the view's reload does to the screen:
//
//   Periodic   for a reload that keeps what is displayed until the new data
//              lands. Runs every `seconds` while the view is on screen.
//   OnReturn   for a reload that clears the view first ("Loading…"). Never
//              runs under someone who is reading: only when they come BACK to
//              the view — it is shown again, or its window is restored or
//              re-activated — after at least `seconds` since the last load.
//
// Either way a hidden tab or a minimised window spends nothing, and returning
// to a view older than its interval refreshes it at once.
//
// Age is measured on the WALL clock: a monotonic timer does not advance while
// the machine is suspended, and "left open overnight" is mostly a suspended
// laptop. Call ui::mark_refreshed(view) from the view's own load so a manual
// refresh or a symbol change also resets the age.
//
// Views that read through QueryStore do not need this: the store revalidates
// them against their TTL.

#include <QDateTime>
#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <functional>

namespace finterm::ui {

class VisibleRefresh : public QObject {
  public:
    enum class Mode { Periodic, OnReturn };
    static constexpr const char* kName = "fintermVisibleRefresh";

    VisibleRefresh(QWidget* target, int seconds, Mode mode, std::function<void()> fn)
        : QObject(target), target_(target), interval_s_(seconds), mode_(mode), fn_(std::move(fn)) {
        setObjectName(QLatin1String(kName));
        touch();
        target_->installEventFilter(this);
        // Restoring or re-activating a window sends its events to the window,
        // not to the child view. The window can change (a tile re-docked into
        // a floating panel), so it is looked up again on every poll.
        watch_window();
        // A short poll against the wall clock, not one long timer: it notices
        // a wake from suspend and a restored window within half a minute.
        poll_.setInterval(30 * 1000);
        connect(&poll_, &QTimer::timeout, this, [this]() {
            watch_window();
            if (mode_ == Mode::Periodic)
                run_if_due();
        });
        poll_.start();
    }

    /// The view was just loaded (by anyone): its age starts again.
    void touch() { last_ = QDateTime::currentSecsSinceEpoch(); }

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        const QEvent::Type t = event->type();
        const bool returned = (watched == target_ && t == QEvent::Show) ||
                              (watched == window_ && (t == QEvent::WindowActivate || t == QEvent::WindowStateChange));
        if (returned)
            QTimer::singleShot(0, this, [this]() { run_if_due(); });
        return QObject::eventFilter(watched, event);
    }

  private:
    void watch_window() {
        QWidget* w = target_->window();
        if (w == window_ || w == target_)
            return;
        if (window_)
            window_->removeEventFilter(this);
        window_ = w;
        if (window_)
            window_->installEventFilter(this);
    }

    void run_if_due() {
        if (!target_->isVisible() || (target_->window() && target_->window()->isMinimized()))
            return;
        // A clock set backwards makes the age negative: treat that as due.
        const qint64 age = QDateTime::currentSecsSinceEpoch() - last_;
        if (age >= 0 && age < interval_s_)
            return;
        touch();
        fn_();
    }

    QWidget* target_;
    QPointer<QWidget> window_;
    qint64 interval_s_;
    Mode mode_;
    std::function<void()> fn_;
    QTimer poll_;
    qint64 last_ = 0;
};

/// Reload `target` every `seconds` while it is on screen — for a reload that
/// keeps the displayed data until the new data arrives. Owned by `target`.
inline VisibleRefresh* refresh_while_visible(QWidget* target, int seconds, std::function<void()> fn) {
    return new VisibleRefresh(target, seconds, VisibleRefresh::Mode::Periodic, std::move(fn));
}

/// Reload `target` when someone comes back to it after `seconds` or more —
/// for a reload that clears the view first. Owned by `target`.
inline VisibleRefresh* refresh_on_return(QWidget* target, int seconds, std::function<void()> fn) {
    return new VisibleRefresh(target, seconds, VisibleRefresh::Mode::OnReturn, std::move(fn));
}

/// Tell `target`'s refresher (if it has one) that the view was just loaded.
inline void mark_refreshed(QWidget* target) {
    if (auto* o = target->findChild<QObject*>(QLatin1String(VisibleRefresh::kName), Qt::FindDirectChildrenOnly))
        static_cast<VisibleRefresh*>(o)->touch();
}

} // namespace finterm::ui

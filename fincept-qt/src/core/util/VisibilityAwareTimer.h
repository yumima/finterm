#pragma once
#include <QObject>
#include <QTimer>

class QWidget;

namespace fincept::util {

/// A QTimer that automatically pauses while its watched widget isn't visible.
///
/// Background: most screens spawn their own QTimers for refreshes, polls,
/// animations, status ticks, etc. When the parent dock tab is switched away
/// or the main window is hidden, the widget is no longer drawn — but its
/// timers keep firing on the GUI thread, doing work that no one can see.
/// This widget-by-widget oversight accumulates into measurable event-loop
/// pressure (10–40 events/sec on a busy dashboard) that competes with input.
///
/// Drop-in replacement for QTimer at any screen / panel / widget level.
/// Pass the widget whose visibility should gate the timer; the helper
/// installs an event filter and pauses on Hide, resumes on Show — but only
/// if the caller had previously called start(). stop() persistently
/// suppresses the timer until start() is called again.
///
/// Lifetimes: the helper QObject-parents itself to `watched` so it shares
/// the widget's lifetime; the caller does not need to manage destruction.
class VisibilityAwareTimer : public QObject {
    Q_OBJECT
  public:
    /// `watched` is the widget whose visibility gates this timer.
    explicit VisibilityAwareTimer(QWidget* watched);

    void setInterval(int msec);
    int  interval() const { return timer_.interval(); }
    void setSingleShot(bool single);
    bool isSingleShot() const { return timer_.isSingleShot(); }

    /// Start (or restart) the timer. If the watched widget is currently
    /// hidden, the timer goes into "wanted but paused" state and will
    /// auto-start when the widget next becomes visible.
    void start();
    void start(int msec);

    /// Cancel the timer. After stop() the watcher does not auto-resume on
    /// the next show event; call start() again to re-arm.
    void stop();

    /// True if the underlying QTimer is currently ticking.
    bool isActive() const { return timer_.isActive(); }

    /// True if the caller has requested the timer to be running (it may
    /// still be paused if the widget is hidden).
    bool isWanted() const { return wanted_active_; }

  signals:
    void timeout();

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    QTimer   timer_;
    QWidget* watched_ = nullptr;
    bool     wanted_active_ = false;
};

} // namespace fincept::util

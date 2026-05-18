// src/ui/widgets/StatusPill.h
#pragma once
#include <QLabel>
#include <QString>
#include <QWidget>

#include <functional>

namespace fincept::ui {

/// Shared status-pill widget for panel headers. Renders as a compact:
///
///   ● <message>          [optional ↻ retry button]
///
/// row that any panel can drop into its title bar. Replaces the ad-hoc
/// QLabel-with-status-text pattern many panels reinvented, with two
/// concrete UX wins over a plain label:
///
///   1. Distinct visual states. The user can glance at the dot colour
///      and know whether the panel is idle / loading / showing stale
///      data / errored — without reading the text. Error states stand
///      out instead of looking like every other muted-grey caption.
///
///   2. Retry affordance. When set_error(msg, [retry]) is called with a
///      retry callback, an inline ↻ button appears next to the message.
///      Clicking it invokes the callback. Eliminates the "panel shows
///      'error' in grey text and the user has no path back to data"
///      class of dead-end states that several existing panels fall into.
///
/// Lifecycle: parented like any QWidget; the retry callback is captured
/// by value and replaced on each set_error call, so callers don't have
/// to track lambda lifetimes.
class StatusPill : public QWidget {
    Q_OBJECT
  public:
    enum class State {
        Idle,         ///< Steady-state success — e.g. "yfinance · 15:34:01"
        Loading,      ///< Fetch in flight — e.g. "loading…"
        Stale,        ///< Data showing but stale — SWR window, amber dot
        Error,        ///< Last fetch failed — red dot, retry button shown
        Empty,        ///< Loaded successfully, no data — neutral message
    };

    explicit StatusPill(QWidget* parent = nullptr);

    /// Convenience setters — pick one per fetch transition. Each updates
    /// the dot colour, message text, and retry-button visibility in a
    /// single call so callers don't have to coordinate three properties.
    void set_idle(const QString& message);
    void set_loading(const QString& message = QStringLiteral("loading…"));
    void set_stale(const QString& message);
    void set_error(const QString& message,
                   std::function<void()> on_retry = {});
    void set_empty(const QString& message);

  signals:
    /// Emitted when the user clicks the retry button. Most callers will
    /// pass an on_retry callback to set_error directly (it wires this
    /// signal internally); this signal is exposed for callers that want
    /// to multiplex retry into a broader refresh path.
    void retry_requested();

  private:
    void apply_state(State s, const QString& message);

    QLabel*    dot_      = nullptr;   // unicode bullet, colour by state
    QLabel*    label_    = nullptr;   // status message
    QWidget*   retry_btn_= nullptr;   // ↻ button — hidden unless Error+callback
    std::function<void()> on_retry_;
    State      state_ = State::Idle;
};

} // namespace fincept::ui

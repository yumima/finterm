// src/services/markets/ExtendedHoursService.h
#pragma once

#include "screens/dashboard/widgets/ExtendedHoursMath.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <optional>

namespace finterm::services {

struct StreamTick;

/// The one source of every holding's extended-hours move (AFT%).
///
/// The dashboard's Portfolio Summary, the Portfolio heatmap and the Portfolio
/// futures view used to fetch `extended_hours` each on their own — three 5-day
/// 1-minute downloads of the same book, on three schedules, with three merge
/// rules. When Yahoo rate-limited the app's first minute (2026-11-17), the
/// dashboard re-polled and recovered while the heatmap kept its blanks: same
/// symbol, two answers on screen. Now there is one fetch for the union of what
/// is on screen, one rule (exthours::apply_row / apply_print), one stream
/// subscription, and every surface just renders.
class ExtendedHoursService : public QObject {
    Q_OBJECT
  public:
    using Quote = finterm::screens::widgets::exthours::HeldQuote;
    enum class State { Idle, Loading, Failed };

    static ExtendedHoursService& instance();

    /// What `owner` shows (empty = nothing; call on hide). Released on destroy.
    /// Symbols nobody has asked for yet are fetched straight away.
    void watch(QObject* owner, const QStringList& symbols);

    /// The shown move for `symbol`, if there is one.
    std::optional<Quote> quote(const QString& symbol) const;
    /// The last regular close the fetch reported, or 0 — a row with no move
    /// still has a close to show.
    double regular_close(const QString& symbol) const { return regular_.value(symbol, 0.0); }

    State state() const { return state_; }
    QString error() const { return error_; }
    QDateTime last_answer() const { return last_answer_; }

    /// Ask again now (a user's retry), unless a fetch is already out.
    void refresh();

  signals:
    /// These symbols' moves changed (one emission per fetch / print batch).
    void quotes_changed(const QSet<QString>& symbols);
    void state_changed();

  private:
    ExtendedHoursService();
    void recompute_watched();
    void schedule_fetch();
    /// One full fetch of the watched set — never two at once. `force`
    /// skips the "could a fetch add anything" gate, not the in-flight one.
    void fetch(bool force = false);
    bool fetch_could_help() const;
    void on_ticks(const QVector<StreamTick>& ticks);
    void set_state(State s, const QString& error = {});
    void load_snapshot();
    void save_snapshot() const;

    QHash<QObject*, QStringList> owners_;
    QSet<QObject*> hooked_;        // owners whose destroyed() is connected (once)
    QStringList watched_;          // sorted union of owners_
    QSet<QString> watched_set_;
    QSet<QString> covered_;        // symbols the last good fetch answered
    QSet<QString> owed_;           // refused last time: ask again
    QHash<QString, Quote> quotes_;
    QHash<QString, double> regular_;
    QSet<QString> declined_;
    State state_ = State::Idle;
    QString error_;
    QDateTime last_answer_;
    bool in_flight_ = false;
    // Consecutive fetches that failed or left symbols refused. Outside an
    // extended session they are re-asked on a back-off, not every poll.
    int refusal_streak_ = 0;
    QDateTime last_attempt_;
    bool kick_pending_ = false;
    QTimer timer_;
};

} // namespace finterm::services

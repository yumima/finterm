// src/services/equity/EarningsSignal.h
#pragma once
#include "services/equity/EquityResearchModels.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace fincept::services::equity {

/// The pre-earnings outlook: three questions, each answered only as far as
/// the evidence goes.
///
///   WILL THEY BEAT?  A probability, and how likely each size of surprise is.
///                    Predictable: a name's own beat record, shrunk toward the
///                    pooled rate, beats the pooled rate alone out of sample.
///   HOW BIG A MOVE?  A one-session size forecast, with the ranges it
///                    actually achieves. Predictable: a name's print-day size
///                    is persistent and recent volatility sharpens it.
///   WHICH WAY?       Not predictable from anything available before the print,
///                    so no call is made. What IS known is what each outcome
///                    has meant for the price, in units of this name's own
///                    expected move, and that is what is shown.
///
/// This replaces a hand-weighted BUY / HOLD / SELL scorecard. Measured
/// walk-forward over 9,665 prints on 236 US names (2014–2026), that scorecard
/// called the direction right 50.7% of the time — below the 50.9% of "always
/// up" — and its SELL calls averaged +0.56% on the day, the best of the three.
/// No backward-looking leg it used carried a usable sign (every |IC| under
/// 0.03), and the probability of a rise implied by the scenarios below does not
/// beat a coin flip either (Brier 0.2505 vs 0.2500). The study is in
/// plans/research/earnings_backtest_2026-10.md.
///
/// The engine lives in the service layer (not the tab) so it stays free of Qt
/// widget dependencies and can be unit-tested against fixed inputs.

/// One way the print can land, by EPS surprise against consensus.
struct EarningsScenario {
    QString label;              // "MISS", "SLIGHT BEAT", …
    QString range;              // "below consensus", "0 – 3%", …
    double  probability = 0.0;  // 0 … 1, this name's record shrunk to the pooled mix
    int     name_count = 0;     // how many of the name's scored quarters landed here
    /// Pooled mean next-session move for this outcome, in multiples of the
    /// name's own expected move. Stable across both halves of the sample;
    /// per-name versions are not (persistence IC −0.01), which is why the
    /// pooled figure is used and then scaled to this name's size.
    double  move_multiple = 0.0;
    double  typical_move_pct = 0.0;  // move_multiple × expected move
    double  up_rate = 0.0;           // pooled share of these prints that rose
};

/// Which way the street has been moving its numbers. Context for the beat
/// question — shown, never folded into the probability, because Yahoo
/// publishes it as a snapshot with no history to test it against.
enum class EstimateDrift { Rising, Flat, Falling, Unknown };

struct EarningsOutlook {
    bool valid = false;               // false: nothing earnings-shaped to say
    QString headline;                 // the three answers in one sentence
    int days_to_report = -1;          // -1 when no date is published

    // ── 1. Will they beat? ──────────────────────────────────────────────────
    std::optional<double> p_beat;     // 0 … 1
    double pooled_beat_rate = 0.0;    // what an unknown name would be given
    int    beats = 0;                 // in the scored window
    int    scored_quarters = 0;       // quarters with a usable surprise
    std::optional<double> typical_surprise_pct;   // median of the scored window
    QVector<EarningsScenario> scenarios;          // ordered worst → best
    EstimateDrift drift = EstimateDrift::Unknown;
    QString drift_detail;             // "+2.1% in 90 days · 4 up / 0 down in 30"
    std::optional<double> dispersion_pct;   // (high−low)/|mean| on this quarter's EPS
    bool dispersion_is_wide = false;

    // ── 2. How big a move? ──────────────────────────────────────────────────
    std::optional<double> expected_move_pct;   // one session, either direction
    std::optional<double> trailing_move_pct;   // plain mean |move| over past prints
    int    reaction_quarters = 0;
    // Ranges the forecast actually achieves, in percent. Half of past prints
    // pooled landed inside `half_within`, four in five inside `most_within`,
    // and one in ten beyond `tail_beyond`.
    std::optional<double> half_within_pct;
    std::optional<double> most_within_pct;
    std::optional<double> tail_beyond_pct;
    std::optional<double> implied_move_pct;    // options event move, when separable
    /// implied ÷ expected. Above 1 the options are pricing a bigger session
    /// than this name's history and current volatility suggest.
    std::optional<double> implied_ratio;

    // ── 3. Which way? — what is already priced in (context only) ────────────
    QStringList priced_in;            // plain-English lines; never a call

    QStringList caveats;
};

/// Build the outlook. Safe on an empty/invalid analysis.
EarningsOutlook evaluate_outlook(const EarningsAnalysis& a);

/// One past print, with the forecasts that stood before it — rebuilt from the
/// quarters before it only, so nothing is informed by its own outcome.
struct PrintForecast {
    qint64 timestamp = 0;
    std::optional<double> expected_move_pct;   // size forecast before the print
    std::optional<double> actual_move_pct;     // realised close-to-close
    std::optional<double> p_beat;              // beat probability before the print
    std::optional<bool>   beat;                // what happened (unset when suspect)
};

/// How this name's own forecasts have fared, walk-forward.
struct ForecastRecord {
    QVector<PrintForecast> prints;   // oldest first, aligned with the chart
    // Size: share of prints that landed inside the expected move (pooled
    // expectation is about half), and mean miss against the plain trailing
    // average — the baseline the blend has to beat to be worth having.
    int    size_graded = 0;
    int    size_inside = 0;
    double size_mae = 0.0;
    double trailing_mae = 0.0;
    // Beat: mean stated probability against the realised rate.
    int    beat_graded = 0;
    int    beat_hits = 0;
    double mean_p_beat = 0.0;
};

ForecastRecord forecast_record(const EarningsAnalysis& a);

/// Which earnings metric the chart bars show.
enum class ReactionMetric { Surprise, QoQ, YoY };

/// Measured relationship between one earnings metric and the close-to-close
/// move over the print. Descriptive only — these metrics exist after the
/// print, so they explain moves rather than predict them.
struct ReactionCorrelation {
    ReactionMetric metric = ReactionMetric::Surprise;
    QString label;                 // "SURPRISE", "QoQ", "YoY"
    std::optional<double> r;       // Pearson, -1 … +1; unset when n < 3
    int n = 0;
};

QVector<ReactionCorrelation> correlate_reactions(const EarningsAnalysis& a);

/// The metric value on one quarter, for whichever metric is selected.
std::optional<double> metric_value(const EarningsPoint& p, ReactionMetric m);

/// Which scenario a realised surprise falls in (index into
/// EarningsOutlook::scenarios), or -1 for none / an accounting artefact.
int scenario_index(const EarningsPoint& p);

/// Calendar days from today until the next report, counted in US market time
/// (the exchange session is what "reports Thursday after the close" refers to,
/// and a viewer west of ET would otherwise be handed the wrong day). Negative
/// when the date has passed; -1 also means "no date published", which callers
/// separate by checking `a.next.timestamp` first.
int days_to_next_earnings(const EarningsAnalysis& a);

/// Same, against a caller-supplied clock, so it can be tested at a fixed time.
int days_to_next_earnings(const EarningsAnalysis& a, const QDateTime& now);

} // namespace fincept::services::equity

// src/services/equity/EarningsSignal.h
#pragma once
#include "services/equity/EquityResearchModels.h"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::services::equity {

/// Rules-based pre-earnings scorecard.
///
/// Turns an EarningsAnalysis into a transparent BUY / HOLD / SELL read on the
/// *setup into the next report* — deliberately not a valuation opinion and not
/// a price forecast. Every component is scored independently in [-1, +1],
/// weighted, and reported alongside the number it was derived from, so the
/// user can disagree with any single leg and see exactly how much it moved the
/// verdict. Components with no data drop out of the weighted average and
/// reduce `confidence` rather than silently scoring zero.
///
/// Weights are not fixed: they rotate with the time left before the print. Six
/// weeks out the only legs with anything to say are the slow ones — the track
/// record, the expected growth, where the price already sits. Inside the final
/// week those are stale and the fast legs (which way consensus is moving, who
/// is revising, what is happening to the *next* quarter's number) carry the
/// read. `horizon_note` states which way the rotation currently leans.
///
/// The engine lives in the service layer (not the tab) so it stays free of Qt
/// widget dependencies and can be unit-tested against fixed inputs.

enum class SignalDirection { Bullish, Neutral, Bearish };

/// How fast a leg's information decays.
///
/// Short-horizon legs (what analysts are doing to their numbers right now) are
/// nearly silent two months out and are most of the signal in the final week.
/// Long-horizon legs (track record, expected growth, where the price sits) say
/// the same thing all quarter, which makes them the only thing worth reading
/// far out and the least worth re-reading the night before. The composite
/// rotates weight between the two as the date approaches.
enum class SignalHorizon { Short, Long };

/// One scored leg of the verdict.
struct SignalComponent {
    QString name;             // "REVISION MOMENTUM"
    QString detail;           // "Current-qtr EPS +8.7% vs 90d ago"
    QString explanation;      // why this leg matters, for the tooltip
    double  score = 0.0;      // -1 … +1
    double  weight = 0.0;     // share of the composite AFTER horizon rotation
    double  base_weight = 0.0;// share before rotation — what the leg is worth in principle
    SignalHorizon horizon = SignalHorizon::Long;
    bool    available = false;
};

struct EarningsVerdict {
    SignalDirection direction = SignalDirection::Neutral;
    QString label;                      // "BUY" / "HOLD" / "SELL"
    double  score = 0.0;                // -100 … +100
    double  confidence = 0.0;           // 0 … 1 — share of weight that had data
    QString headline;                   // one-line plain-English summary
    QStringList caveats;                // risk notes that don't move the score
    QVector<SignalComponent> components;
    /// Plain-English statement of how far out the report is and which legs the
    /// rotation is currently favouring. Always set — a reader who can't see why
    /// the weights moved would reasonably assume they were fixed.
    QString horizon_note;
    int     days_to_report = -1;        // -1 when the date is unknown

    // Descriptive stats the UI shows next to the verdict.
    int    scored_quarters = 0;         // reported quarters with a surprise
    double beat_rate = 0.0;             // 0 … 1
    double avg_surprise_pct = 0.0;
    double surprise_stdev_pct = 0.0;    // quarter-to-quarter spread of the surprise
    std::optional<double> beat_reaction_pct;  // mean move on quarters that beat
    int    reaction_quarters = 0;
    double avg_reaction_pct = 0.0;      // signed mean 1-day move
    double typical_move_pct = 0.0;      // mean |1-day move| — the expected move
    double up_reaction_rate = 0.0;      // 0 … 1
    /// Analyst spread on the coming quarter's EPS, (high−low) as a percentage
    /// of the mean. A risk gauge, never a direction: a wide consensus means a
    /// bigger surprise in whichever direction it lands, so it is reported and
    /// caveated but deliberately kept out of the score.
    std::optional<double> dispersion_pct;
    /// `dispersion_pct` past the threshold the engine calls wide. Carried as a
    /// flag so the panel and the caveat can't drift apart on where the line is.
    bool dispersion_is_wide = false;
};

/// Score `a`. Safe on an empty/invalid analysis — returns a Neutral verdict
/// with zero confidence and an explanatory headline.
EarningsVerdict evaluate_earnings(const EarningsAnalysis& a);

/// Which earnings metric the next-day move actually tracked, for THIS name.
enum class ReactionMetric { Surprise, QoQ, YoY };

/// Measured relationship between one earnings metric and the close-to-close
/// move over the print. Reported, never scored: a dozen quarters is far too
/// few for significance, and the honest use of the number is "does this stock
/// tend to trade off this input at all", not "predict the next move".
struct ReactionCorrelation {
    ReactionMetric metric = ReactionMetric::Surprise;
    QString label;                 // "SURPRISE", "QoQ", "YoY"
    std::optional<double> r;       // Pearson, -1 … +1; unset when n < 3
    int n = 0;                     // quarters with both values present
};

/// Correlate all three metrics against the realised reaction.
QVector<ReactionCorrelation> correlate_reactions(const EarningsAnalysis& a);

/// The metric value on one quarter, for whichever metric is selected.
std::optional<double> metric_value(const EarningsPoint& p, ReactionMetric m);

/// Calendar days from today until the next report, counted in US market time
/// (the exchange session is what "reports Thursday after the close" refers to,
/// and a viewer west of ET would otherwise be handed the wrong day). Negative
/// when the date has passed; -1 also means "no date published", which callers
/// separate by checking `a.next.timestamp` first.
int days_to_next_earnings(const EarningsAnalysis& a);

/// Same, against a caller-supplied clock. The engine's own horizon weighting
/// goes through this so the rotation can be tested at a fixed point in time.
int days_to_next_earnings(const EarningsAnalysis& a, const QDateTime& now);

} // namespace fincept::services::equity

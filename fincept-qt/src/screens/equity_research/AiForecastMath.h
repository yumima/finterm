// src/screens/equity_research/AiForecastMath.h
#pragma once
#include <QString>

#include <algorithm>
#include <cmath>
#include <optional>

/// Pure rules behind the AI Forecast tab's numbers. Kept free of Qt widget
/// types, and of the LLM transport, so they can be tested against fixed inputs
/// without standing up a UI or a model.
///
/// Everything here exists because a language model's structured output is not
/// self-consistent. Asked for a direction, a target price and a percentage, it
/// will happily return "up", "$108" and "+3%" for a $100 stock — three claims,
/// at most one of which can be right. The tab used to store all three verbatim,
/// after which the table's DIRECTION column, its TARGET column, the chart's
/// segment and the accuracy metric each quoted a different one while the panel
/// presented them as a single forecast.
namespace fincept::screens::ai_forecast {

/// Moves smaller than this (in percent) are "flat" rather than up or down.
///
/// One constant, used for BOTH the prediction and the outcome. It was applied
/// to the outcome only: a stated "up" worth +0.3% was scored against a realised
/// +0.4% that bucketed to "flat", and marked wrong for landing where it said.
inline constexpr double kFlatBandPct = 1.0;

/// Beyond this, a "forecast" is a slip rather than a call.
///
/// Not a view on what a stock can do — it is a bound on what a MODEL's number
/// is allowed to do to a permanent record. A $2,500 target on a $320 stock
/// (+681%) is a units error or a confused security, and because the tab's
/// "avg miss" metric averages |predicted − actual| over immutable rows, one
/// such row moves that average by hundreds of points for that ticker forever.
inline constexpr double kMaxPlausibleMovePct = 100.0;

inline QString bucket_direction(double pct, double flat_band = kFlatBandPct) {
    if (pct > flat_band) return QStringLiteral("up");
    if (pct < -flat_band) return QStringLiteral("down");
    return QStringLiteral("flat");
}

/// One coherent forecast, derived from whatever the model actually returned.
struct Forecast {
    double  target_price = 0.0;
    double  predicted_pct = 0.0;
    QString direction = QStringLiteral("flat");
    /// The model's own figures contradicted each other by more than rounding.
    /// Carried so the tab can say so — it is the cheapest available signal that
    /// the model was sloppy on this run, and it vanishes once reconciled.
    bool    incoherent = false;
    /// Nothing usable was returned: no target, no percentage, no anchor price —
    /// or every figure offered was outside kMaxPlausibleMovePct. The caller MUST
    /// check this and decline to record; a Forecast that is empty carries a
    /// direction of "flat", and storing that would put a call the model never
    /// made into a track record that is then graded against real closes.
    bool    empty = true;
};

/// Reconcile a model's target / percentage / direction against the price we
/// recorded ourselves.
///
/// `price_now` is OURS and exact, so the arithmetic tying a target to a
/// percentage is arithmetic we can do correctly. The target wins where both are
/// present — it is what the chart draws and what a reader anchors on — and the
/// percentage is derived from it, so the line on the chart and the error in the
/// table are guaranteed to be the same claim. The direction is derived last,
/// from that percentage, so a prediction and an outcome are bucketed by one
/// rule.
///
/// The model's own `direction` word is deliberately not consulted. It is
/// redundant when it agrees and wrong when it does not, and there is no way to
/// tell which of a contradictory pair the model "meant".
/// `stated_pct` is optional on purpose: QJsonValue::toDouble() returns 0 for a
/// MISSING field and for a genuine zero alike, and the two mean opposite things
/// here. "the model said flat" contradicts an 8% target and must be reported;
/// "the model said nothing" has nothing to contradict.
inline Forecast reconcile(double price_now, double stated_target,
                          std::optional<double> stated_pct,
                          double flat_band = kFlatBandPct) {
    Forecast f;
    if (!(price_now > 0.0))
        return f;   // no anchor; nothing here can be made coherent

    const auto plausible = [](double pct) { return std::abs(pct) <= kMaxPlausibleMovePct; };
    const auto from_pct = [&](double pct) {
        f.predicted_pct = pct;
        f.target_price  = price_now * (1.0 + pct / 100.0);
        f.empty = false;
    };

    if (stated_target > 0.0) {
        const double derived = (stated_target - price_now) / price_now * 100.0;
        if (plausible(derived)) {
            f.target_price  = stated_target;
            f.predicted_pct = derived;
            f.empty = false;
            // Rounding is fine — a model quoting "+3.2%" against a target that
            // works out to 3.24% has not contradicted itself. Half a point, or
            // a tenth of the move on a big call, has.
            if (stated_pct)
                f.incoherent = std::abs(*stated_pct - derived) >
                               std::max(0.5, 0.10 * std::abs(derived));
        } else if (stated_pct && plausible(*stated_pct)) {
            // The target is not a forecast. Fall back to the percentage, which
            // is the model's other claim about the same move, and say the two
            // disagreed — deriving from the target here is what would put +681
            // into an average that can never be corrected.
            from_pct(*stated_pct);
            f.incoherent = true;
        }
        // Both absurd: nothing usable, and `empty` stays true.
    } else if (stated_pct && plausible(*stated_pct)) {
        // A stated 0 is a real flat call, not a missing field — see above.
        from_pct(*stated_pct);
    }

    if (!f.empty)
        f.direction = bucket_direction(f.predicted_pct, flat_band);
    return f;
}

} // namespace fincept::screens::ai_forecast

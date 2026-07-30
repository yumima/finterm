// src/services/equity/EarningsSignal.h
#pragma once
#include "services/equity/EquityResearchModels.h"

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
/// The engine lives in the service layer (not the tab) so it stays free of Qt
/// widget dependencies and can be unit-tested against fixed inputs.

enum class SignalDirection { Bullish, Neutral, Bearish };

/// One scored leg of the verdict.
struct SignalComponent {
    QString name;             // "REVISION MOMENTUM"
    QString detail;           // "Current-qtr EPS +8.7% vs 90d ago"
    QString explanation;      // why this leg matters, for the tooltip
    double  score = 0.0;      // -1 … +1
    double  weight = 0.0;     // share of the composite
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

    // Descriptive stats the UI shows next to the verdict.
    int    scored_quarters = 0;         // reported quarters with a surprise
    double beat_rate = 0.0;             // 0 … 1
    double avg_surprise_pct = 0.0;
    int    reaction_quarters = 0;
    double avg_reaction_pct = 0.0;      // signed mean 1-day move
    double typical_move_pct = 0.0;      // mean |1-day move| — the expected move
    double up_reaction_rate = 0.0;      // 0 … 1
};

/// Score `a`. Safe on an empty/invalid analysis — returns a Neutral verdict
/// with zero confidence and an explanatory headline.
EarningsVerdict evaluate_earnings(const EarningsAnalysis& a);

/// Days from now until the next report; -1 when unknown.
int days_to_next_earnings(const EarningsAnalysis& a);

} // namespace fincept::services::equity

// src/ui/components/SignalTooltip.cpp
#include "ui/components/SignalTooltip.h"

#include "screens/power_trader/PowerTraderTypes.h"
#include "ui/theme/Theme.h"

namespace fincept::ui {

namespace {

// Mirror of compute_signal_score() in
//   scripts/senate_disclosures_data.py
// Keep these in sync. The Python is the source of truth — this helper exists
// only so the tooltip can break the cooked score back down for the user.

// Component caps, as scored: 30 + 15 + 10 = 55. The published score rescales
// that total to a real 0-100 (see compute_signal_score).
//
// The breakdown is shown on the RAW scale against raw caps, with the raw total
// stated beside the published one. Scaling each row individually looked
// tidier and was wrong: rounding each component separately and summing cannot
// match a score that rounds the TOTAL, and the badge additionally truncates.
// Measured against 942 real trades, 23% showed rows that did not add up to
// their header — the same contradiction the rescale set out to end, in a form
// harder to notice.
constexpr double kRawMax = 55.0;

int committee_component(const power_trader::PoliticalTrade& t) {
    return t.committee_relevance.isEmpty() ? 0 : 30;
    // (The 20-point "ticker→committee mapping" branch is not stored on the
    //  trade in C++; if the C++ trade lacks committee_relevance we can't
    //  reliably tell whether the score nonetheless got the +20 from the
    //  ticker map. The tooltip stays conservative and says 0 there.)
}

int timing_component(int lag_days) {
    if (lag_days > 40) return 15;
    if (lag_days > 30) return 8;
    if (lag_days > 20) return 3;
    return 0;
}

int size_component(double amount_high) {
    if (amount_high >= 1'000'000.0) return 10;
    if (amount_high >=   250'000.0) return 5;
    if (amount_high >=    50'000.0) return 2;
    return 0;
}

QString row(const QString& label, int contributed, int weight, const QString& detail) {
    const QString val_color = contributed > 0
        ? QString::fromLatin1(ui::colors::AMBER())
        : QString::fromLatin1(ui::colors::TEXT_SECONDARY());
    return QStringLiteral(
        "<tr>"
          "<td style='padding-right:14px;color:%1;'>%2</td>"
          "<td align='right' style='padding-right:8px;color:%3;font-weight:700;'>+%4</td>"
          "<td align='right' style='color:%1;'>/ %5</td>"
          "<td style='padding-left:14px;color:%1;'>%6</td>"
        "</tr>")
        .arg(QString::fromLatin1(ui::colors::TEXT_SECONDARY()),
             label, val_color, QString::number(contributed),
             QString::number(weight), detail);
}

} // namespace

QString tooltip_for_trade_signal(const power_trader::PoliticalTrade& t) {
    const int cmte_pts   = committee_component(t);
    const int timing_pts = timing_component(t.disclosure_lag_days);
    const int size_pts   = size_component(t.amount_high);

    const QString cmte_detail = t.committee_relevance.isEmpty()
        ? QStringLiteral("no committee match")
        : t.committee_relevance;
    const QString timing_detail =
        QStringLiteral("%1d to disclose").arg(t.disclosure_lag_days);
    const QString size_detail = t.amount_high >= 1'000'000.0
        ? QStringLiteral("≥ $1M")
        : t.amount_high >= 250'000.0 ? QStringLiteral("≥ $250K")
        : t.amount_high >=  50'000.0 ? QStringLiteral("≥ $50K")
        : QStringLiteral("< $50K");

    return QStringLiteral(
        "<div style='font-family:Consolas,monospace;'>"
          "<div style='font-weight:700;color:%1;letter-spacing:0.5px;"
                     "margin-bottom:4px;'>SIGNAL SCORE  ·  %2 / 100</div>"
          "<table cellspacing='0' cellpadding='0'>%3%4%5</table>"
          "<div style='color:%6;margin-top:6px;font-size:11px;'>"
              "Higher = stronger insider signal · "
              "<i>Educational use only — not investment advice.</i>"
          "</div>"
        "</div>")
        .arg(QString::fromLatin1(ui::colors::AMBER()),
             // Header states both scales, so the rows below always reconcile
             // against one of them exactly.
             QStringLiteral("%1  <span style='font-weight:400;'>(%2 of %3 pts)</span>")
                 .arg(QString::number(qRound(t.signal_score)))
                 .arg(cmte_pts + timing_pts + size_pts)
                 .arg(static_cast<int>(kRawMax)),
             row(QStringLiteral("Committee overlap"), cmte_pts,   30, cmte_detail),
             row(QStringLiteral("Disclosure timing"), timing_pts, 15, timing_detail),
             row(QStringLiteral("Trade size"),        size_pts,   10, size_detail),
             QString::fromLatin1(ui::colors::TEXT_SECONDARY()));
}

QString tooltip_for_peak_signal(int n_trades) {
    return QStringLiteral(
        "<div style='font-family:Consolas,monospace;'>"
          "<div style='font-weight:700;color:%1;letter-spacing:0.5px;"
                     "margin-bottom:4px;'>PEAK SIGNAL</div>"
          "<div style='color:%2;'>Highest per-trade score across this"
            " member's %3 committee-relevant trade%4."
            "<br/>Per-trade points: committee overlap (≤30), disclosure"
            " timing (≤15), trade size (≤10), rescaled from 55 to 0–100."
            " A hand-set composite of three disclosed inputs, not a"
            " measurement.</div>"
          "<div style='color:%2;margin-top:6px;font-size:11px;'>"
              "<i>Educational use only — not investment advice.</i>"
          "</div>"
        "</div>")
        .arg(QString::fromLatin1(ui::colors::AMBER()),
             QString::fromLatin1(ui::colors::TEXT_SECONDARY()),
             QString::number(n_trades),
             n_trades == 1 ? QString() : QStringLiteral("s"));
}

QString tooltip_for_aggregate_signal(int n_trades) {
    const QString sample = n_trades >= 0
        ? QStringLiteral("<div style='color:%1;'>Averaged over %2 trade%3.</div>")
              .arg(QString::fromLatin1(ui::colors::TEXT_SECONDARY()),
                   QString::number(n_trades),
                   n_trades == 1 ? QString() : QStringLiteral("s"))
        : QString();

    return QStringLiteral(
        "<div style='font-family:Consolas,monospace;'>"
          "<div style='font-weight:700;color:%1;letter-spacing:0.5px;"
                     "margin-bottom:4px;'>AVERAGE SIGNAL</div>"
          "<div style='color:%2;'>Mean of per-trade scores (0–100)."
            "<br/>Each trade scores: committee overlap (≤30), disclosure"
            " timing (≤15), trade size (≤10), rescaled from 55 to 0–100.</div>"
          "%3"
          "<div style='color:%2;margin-top:6px;font-size:11px;'>"
              "<i>Educational use only — not investment advice.</i>"
          "</div>"
        "</div>")
        .arg(QString::fromLatin1(ui::colors::AMBER()),
             QString::fromLatin1(ui::colors::TEXT_SECONDARY()),
             sample);
}

} // namespace fincept::ui

// src/screens/fingpt/FinGptParse.h
#pragma once
// Parsing the FinGPT-Forecaster answer format — port of
// fingpt/FinGPT_Forecaster/utils.py (parse_answer / map_bin_label), tolerant
// of a zero-shot chat model's drift around the exact markers.
//
// The expected shape (forced by forecaster_system_prompt()):
//   [Positive Developments]:
//   1. ...
//   [Potential Concerns]:
//   1. ...
//   [Prediction & Analysis]
//   Prediction: up by 2-3%
//   Analysis: ...
//
// Nothing here fabricates: a section that cannot be located stays empty and
// `sections_ok` is false, direction 0 means "no direction stated", and the
// bin fields stay NaN when no magnitude was given. The caller decides how to
// present a partial parse (it shows the raw text).

#include "ai_chat/ThinkTrace.h"

#include <QRegularExpression>
#include <QString>

#include <cmath>
#include <limits>
#include <utility>

namespace fincept::screens::fingpt {

struct ForecasterAnswer {
    QString positives;    // body of [Positive Developments]
    QString concerns;     // body of [Potential Concerns]
    QString prediction;   // the "Prediction:" line's text
    QString analysis;     // the "Analysis:" text (or the whole tail)
    bool sections_ok = false;  // all three section markers were found
    int direction = 0;         // +1 up, -1 down, 0 not stated
    // Predicted magnitude band in percent, e.g. 2–3 for "up by 2-3%";
    // "more than 5%" → lo=5, hi=+inf; a single figure → lo==hi; NaN = absent.
    double bin_lo = std::numeric_limits<double>::quiet_NaN();
    double bin_hi = std::numeric_limits<double>::quiet_NaN();
};

/// Remove <think>…</think> traces (closed or truncated-open) that local
/// reasoning models prepend. Defined once in ai_chat/ThinkTrace.h.
inline QString strip_think(QString text) {
    return ai_chat::strip_think_traces(std::move(text));
}

namespace detail {

/// Position just past a section marker like "[Positive Developments]:" —
/// case-insensitive, bracket and trailing colon optional, '&' / "and"
/// both accepted. Returns -1 when absent. `marker_start` receives the
/// match's own start (for slicing the PREVIOUS section's body).
///
/// Line-anchored (leading markdown decoration tolerated): the closing
/// instruction itself says "analyze the positive developments and potential
/// concerns", models echo that sentence as a preamble, and an unanchored
/// match sliced the answer at the echo instead of the section header.
inline int find_marker(const QString& text, const char* core, int from, int* marker_start) {
    const QRegularExpression re(
        QStringLiteral("^[ \\t*#>-]*\\[?\\s*%1\\s*\\]?\\s*:?").arg(QLatin1String(core)),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(text, from);
    if (!m.hasMatch())
        return -1;
    if (marker_start)
        *marker_start = static_cast<int>(m.capturedStart());
    return static_cast<int>(m.capturedEnd());
}

} // namespace detail

inline ForecasterAnswer parse_forecaster_answer(const QString& raw) {
    ForecasterAnswer a;
    const QString text = strip_think(raw);

    int pos_body = detail::find_marker(text, "Positive\\s+Developments", 0, nullptr);
    int con_start = -1;
    int con_body = detail::find_marker(text, "Potential\\s+Concerns",
                                       pos_body < 0 ? 0 : pos_body, &con_start);
    int pred_start = -1;
    int pred_body = detail::find_marker(text, "Prediction\\s*(?:&|and)\\s*Analysis",
                                        con_body < 0 ? 0 : con_body, &pred_start);

    if (pos_body >= 0 && con_start > pos_body)
        a.positives = text.mid(pos_body, con_start - pos_body).trimmed();
    if (con_body >= 0) {
        const int end = (pred_start > con_body) ? pred_start : text.size();
        a.concerns = text.mid(con_body, end - con_body).trimmed();
    }

    QString tail = (pred_body >= 0) ? text.mid(pred_body) : QString();
    a.sections_ok = (pos_body >= 0 && con_body >= 0 && pred_body >= 0);

    if (!tail.isEmpty()) {
        const QRegularExpression pred_re(QStringLiteral("Prediction\\s*:\\s*([^\\n]*)"),
                                         QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression ana_re(QStringLiteral("Analysis\\s*:\\s*"),
                                        QRegularExpression::CaseInsensitiveOption);
        const auto pm = pred_re.match(tail);
        if (pm.hasMatch())
            a.prediction = pm.captured(1).trimmed();
        const auto am = ana_re.match(tail);
        if (am.hasMatch())
            a.analysis = tail.mid(am.capturedEnd()).trimmed();
        else if (pm.hasMatch())
            a.analysis = tail.mid(pm.capturedEnd()).trimmed();
        else
            a.analysis = tail.trimmed();
    }

    // Direction — upstream's vocabulary, but EARLIEST match wins instead of
    // utils.py's up-checked-first. Upstream only ever parsed binned strings
    // ("up by 2-3%"); a chat model's Prediction line is a sentence, and
    // "Moderate decline … becomes bullish if guidance lands" must read as the
    // decline it leads with, not the hedge it ends on. (Observed live: an
    // up-first check painted UP over exactly that answer.)
    const QString dir_src = a.prediction.isEmpty() ? tail : a.prediction;
    // Upstream's vocabulary plus the synonyms chat models actually emit
    // ("Modest Gains (2-5%)" was observed reading as no-direction).
    const QRegularExpression up_re(
        QStringLiteral("\\b(up|increase|rise|bullish|gain|gains|higher|rally|climb|upside|"
                       "appreciate|strengthen|advance)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression down_re(
        QStringLiteral("\\b(down|decrease|decline|fall|bearish|loss|losses|lower|drop|pullback|"
                       "downside|depreciate|weaken|retreat)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    const qsizetype up_at = dir_src.indexOf(up_re);
    const qsizetype down_at = dir_src.indexOf(down_re);
    if (up_at >= 0 && (down_at < 0 || up_at < down_at))
        a.direction = 1;
    else if (down_at >= 0)
        a.direction = -1;

    // Magnitude band (map_bin_label's vocabulary, free-text tolerant).
    const QRegularExpression range_re(
        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*(?:-|–|~|to)\\s*(\\d+(?:\\.\\d+)?)\\s*%"));
    const QRegularExpression more_re(
        QStringLiteral("more\\s+than\\s+(\\d+(?:\\.\\d+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    // The single-figure fallback requires a move-sized preposition: a bare
    // "45%" in the prediction sentence is as likely to be implied volatility
    // or a probability as the week's move, and a confidently wrong "UP by
    // ~45%" header is the exact failure this file exists to avoid. "up 2%"
    // (no preposition, directly after a direction word) is also accepted.
    const QRegularExpression single_re(
        QStringLiteral("\\b(?:by|of|around|about|roughly|approximately|~|up|down|gain|rise|fall|"
                       "decline|appreciate|depreciate)\\s*(\\d+(?:\\.\\d+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    if (const auto m = range_re.match(dir_src); m.hasMatch()) {
        a.bin_lo = m.captured(1).toDouble();
        a.bin_hi = m.captured(2).toDouble();
    } else if (const auto m2 = more_re.match(dir_src); m2.hasMatch()) {
        a.bin_lo = m2.captured(1).toDouble();
        a.bin_hi = std::numeric_limits<double>::infinity();
    } else if (const auto m3 = single_re.match(dir_src); m3.hasMatch()) {
        a.bin_lo = a.bin_hi = m3.captured(1).toDouble();
    }
    return a;
}

/// Human line for the parsed call: "UP by 2–3% over the next week", or an
/// empty string when no direction was stated (caller shows the raw text).
inline QString describe_forecast(const ForecasterAnswer& a) {
    if (a.direction == 0)
        return {};
    QString s = a.direction > 0 ? QStringLiteral("UP") : QStringLiteral("DOWN");
    if (std::isfinite(a.bin_lo)) {
        if (std::isinf(a.bin_hi))
            s += QStringLiteral(" by more than %1%").arg(QString::number(a.bin_lo, 'g', 4));
        else if (a.bin_lo == a.bin_hi)
            s += QStringLiteral(" by ~%1%").arg(QString::number(a.bin_lo, 'g', 4));
        else
            s += QStringLiteral(" by %1–%2%").arg(QString::number(a.bin_lo, 'g', 4),
                                                  QString::number(a.bin_hi, 'g', 4));
    }
    return s;
}

} // namespace fincept::screens::fingpt

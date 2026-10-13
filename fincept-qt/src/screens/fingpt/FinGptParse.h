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
#include <QStringView>
#include <QVector>

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
    int direction = 0;         // +1 up, -1 down, 0 = flat if flat_stated, else not stated
    // The model explicitly called it flat / neutral / sideways. A flat call is
    // a real verdict ("I expect no material move"), not an absent one — the
    // two used to be indistinguishable, so MSFT's "Neutral, 0-1%" rendered as
    // "no direction stated" and was never recorded or scored.
    bool flat_stated = false;
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
    // ("Modest Gains (2-5%)" was observed reading as no-direction). These are
    // the STRONG words — unambiguous direction verbs/nouns.
    static const QRegularExpression up_re(
        QStringLiteral("\\b(up|increase|rise|bullish|gain|gains|higher|rally|climb|upside|"
                       "appreciate|strengthen|advance)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression down_re(
        QStringLiteral("\\b(down|decrease|decline|fall|bearish|loss|losses|lower|drop|pullback|"
                       "downside|depreciate|weaken|retreat)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    // WEAK tier: sentiment adjectives. "Slightly positive (1-2%)" is an up
    // call — but "Despite positive earnings momentum, expect a decline" is
    // not, so these are consulted only when NO strong word appears, and only
    // on an explicit Prediction line (the free tail quotes the [Positive
    // Developments] prose, where they are section echoes, not calls).
    static const QRegularExpression weak_up_re(QStringLiteral("\\bpositive\\b"),
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression weak_down_re(QStringLiteral("\\bnegative\\b"),
                                                 QRegularExpression::CaseInsensitiveOption);
    // An explicit sideways call (plural/variant tolerant — "no significant
    // changes" is one trailing 's' away from the MSFT failure this exists to
    // fix). The common case was a megacap with mixed news: "Prediction:
    // Neutral / flat (0-1%)" carried no up/down word, so a stated verdict
    // rendered as "no direction stated".
    static const QRegularExpression flat_re(
        QStringLiteral("\\b(flat|neutral|sideways|unchanged|range-?bound|stable|"
                       "little\\s+change[d]?|minimal\\s+(?:change|movement)s?|"
                       "no\\s+(?:significant|major|material)\\s+(?:change|movement|move)s?)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    // Concessive markers introduce a HEDGE, not the call ("flat, though
    // upside of 3-5% if guidance beats").
    static const QRegularExpression hedge_re(
        QStringLiteral("\\b(though|although|but|unless|if|however|barring|absent)\\b"),
        QRegularExpression::CaseInsensitiveOption);

    qsizetype up_at = dir_src.indexOf(up_re);
    qsizetype down_at = dir_src.indexOf(down_re);
    if (up_at < 0 && down_at < 0 && !a.prediction.isEmpty()) {
        up_at = dir_src.indexOf(weak_up_re);
        down_at = dir_src.indexOf(weak_down_re);
    }
    const qsizetype flat_at = dir_src.indexOf(flat_re);
    qsizetype dir_at = -1;
    int dir = 0;
    if (up_at >= 0 && (down_at < 0 || up_at < down_at)) {
        dir_at = up_at;
        dir = 1;
    } else if (down_at >= 0) {
        dir_at = down_at;
        dir = -1;
    }
    // Where hedge text begins, when the verdict is flat and a hedged
    // direction follows (-1 = no hedge). A magnitude band found past this
    // point belongs to the hedge, not to the flat call.
    qsizetype hedge_cut = -1;
    if (flat_at >= 0 && (dir_at < 0 || flat_at < dir_at)) {
        if (dir_at < 0) {
            a.flat_stated = true;  // flat with nothing directional anywhere
        } else {
            // Flat led, direction follows. A concessive marker between them
            // makes the direction a hedge and flat the call ("flat, though
            // upside…"); without one, the direction IS the call — both the
            // compound ("Neutral-to-Moderate Decline") and the lead-in clause
            // ("With stable fundamentals, expect the stock to rise 2-3%").
            const QRegularExpressionMatch hm = hedge_re.match(dir_src, flat_at);
            const qsizetype hedge_at = hm.hasMatch() ? hm.capturedStart() : -1;
            if (hedge_at >= 0 && hedge_at < dir_at) {
                a.flat_stated = true;
                hedge_cut = hedge_at;
            } else {
                a.direction = dir;
            }
        }
    } else {
        a.direction = dir;  // may stay 0: nothing stated at all
    }

    // Magnitude band (map_bin_label's vocabulary, free-text tolerant).
    static const QRegularExpression range_re(
        QStringLiteral("(\\d+(?:\\.\\d+)?)\\s*(?:-|–|~|to)\\s*(\\d+(?:\\.\\d+)?)\\s*%"));
    static const QRegularExpression more_re(
        QStringLiteral("more\\s+than\\s+(\\d+(?:\\.\\d+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    // The single-figure fallback requires a move-sized preposition: a bare
    // "45%" in the prediction sentence is as likely to be implied volatility
    // or a probability as the week's move, and a confidently wrong "UP by
    // ~45%" header is the exact failure this file exists to avoid. "up 2%"
    // (no preposition, directly after a direction word) is also accepted.
    static const QRegularExpression single_re(
        QStringLiteral("\\b(?:by|of|around|about|roughly|approximately|~|up|down|gain|rise|fall|"
                       "decline|appreciate|depreciate)\\s*(\\d+(?:\\.\\d+)?)\\s*%"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype band_at = -1;
    if (const auto m = range_re.match(dir_src); m.hasMatch()) {
        a.bin_lo = m.captured(1).toDouble();
        a.bin_hi = m.captured(2).toDouble();
        band_at = m.capturedStart();
    } else if (const auto m2 = more_re.match(dir_src); m2.hasMatch()) {
        a.bin_lo = m2.captured(1).toDouble();
        a.bin_hi = std::numeric_limits<double>::infinity();
        band_at = m2.capturedStart();
    } else if (const auto m3 = single_re.match(dir_src); m3.hasMatch()) {
        a.bin_lo = a.bin_hi = m3.captured(1).toDouble();
        band_at = m3.capturedStart();
    }
    // "Flat, though upside of 3-5%": the 3-5% is the hedge's band. Attaching
    // it to the flat call would advertise a tolerance the model never gave.
    if (a.flat_stated && hedge_cut >= 0 && band_at >= hedge_cut) {
        a.bin_lo = std::numeric_limits<double>::quiet_NaN();
        a.bin_hi = std::numeric_limits<double>::quiet_NaN();
    }
    return a;
}

/// Cut a decoding loop off the tail of a model answer.
///
/// Greedy decoding (temperature 0) on a small local model can collapse into a
/// verbatim word-cycle that fills the whole token budget — observed live on
/// SPCX: the Prediction section degenerated into a ~50-word adverb cycle
/// ("...witnessed universally acknowledged globally recognized...") repeated
/// for pages. max_tokens bounds the damage; this removes it from the display.
///
/// Word-wise period scan: find the smallest period p (in words) whose cycle
/// repeats verbatim to the end of the text for at least two full extra cycles
/// and at least kMinLoopWords words. The cut keeps exactly ONE instance of
/// the cycle (the loop region's seed) and drops the echoes. Prose does not
/// repeat 24+ consecutive words verbatim, so legitimate rhetoric ("very very
/// strong") stays untouched. Returns the (possibly) cut text; *truncated says
/// whether a cut happened — the caller owns how to present that.
inline QString truncate_repetition(const QString& text, bool* truncated = nullptr) {
    if (truncated)
        *truncated = false;
    // Tokenize into words, remembering each word's start offset for the cut.
    QVector<qsizetype> starts;
    QVector<QStringView> words;
    const QStringView v(text);
    qsizetype i = 0;
    while (i < v.size()) {
        while (i < v.size() && v[i].isSpace()) ++i;
        if (i >= v.size()) break;
        const qsizetype b = i;
        while (i < v.size() && !v[i].isSpace()) ++i;
        starts.append(b);
        words.append(v.mid(b, i - b));
    }
    constexpr int kMinLoopWords = 24;  // shortest run that is loop, not rhetoric
    constexpr int kMaxPeriod = 160;    // longest cycle worth hunting (words)
    const int n = static_cast<int>(words.size());
    if (n < kMinLoopWords)
        return text;
    for (int p = 1; p <= kMaxPeriod && p * 3 <= n; ++p) {
        int run = 0;  // trailing words that echo the word one cycle back
        for (int k = n - 1; k - p >= 0 && words[k] == words[k - p]; --k)
            ++run;
        if (run >= 2 * p && run >= kMinLoopWords) {
            if (truncated)
                *truncated = true;
            // [n-run, n) echoes [n-run-p, n-run) — cut at the echoes' start
            // so the seed cycle survives as the text's legitimate ending.
            return text.left(starts[n - run]).trimmed();
        }
    }
    return text;
}

/// Human line for the parsed call: "UP by 2–3%", "FLAT (within ~1%)", or an
/// empty string when no direction was stated (caller shows the raw text).
inline QString describe_forecast(const ForecasterAnswer& a) {
    if (a.direction == 0) {
        if (!a.flat_stated)
            return {};
        QString s = QStringLiteral("FLAT");
        if (std::isfinite(a.bin_lo)) {
            const double bound = std::isinf(a.bin_hi) ? a.bin_lo : a.bin_hi;
            s += QStringLiteral(" (within ~%1%)").arg(QString::number(bound, 'g', 4));
        }
        return s;
    }
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

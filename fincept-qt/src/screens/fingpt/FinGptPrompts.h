// src/screens/fingpt/FinGptPrompts.h
#pragma once
// FinGPT prompt assembly — a faithful port of the AI4Finance FinGPT-Forecaster
// data pipeline (fingpt/FinGPT_Forecaster/app.py) and the FinGPT_Benchmark
// sentiment instruction formats, re-targeted at finterm's own data layer
// (yfinance daemon via EquityResearchService) and whatever chat model the
// "fingpt" role is bound to.
//
// What is kept verbatim: the forecaster system prompt, the weekly price-move
// block wording, the company-introduction template's sentences, the basics
// block framing, the closing instruction, and the sentiment instruction
// strings — the LoRA adapters were trained against these exact shapes, and a
// zero-shot model follows the same structure well (GPT-4 produced FinGPT's own
// training targets from this very prompt).
//
// Deliberate deviations, each for a reason:
//   - News per week is capped at 5 like upstream, but sampled DETERMINISTICALLY
//     (evenly spaced across the week) instead of random.sample(). Upstream's
//     own README warns "Results inferred from randomly chosen news can be
//     strongly biased"; a terminal feature the user re-runs must not change its
//     answer because the dice rolled differently.
//   - Company intro / basics lines are emitted only for fields the vendor
//     actually supplied (NaN-gated). Upstream str.format()s absent finnhub
//     fields straight into the prompt; printing "nan" into a model prompt is
//     the vendor-null-becomes-garbage bug this repo keeps re-fixing.
//   - The sentiment voting templates offer {positive, negative, neutral}.
//     Upstream's multi-template eval dropped neutral rows from the DATASET and
//     offered two options; arbitrary live headlines are routinely neutral and
//     need the third class.

#include "core/util/BarTime.h"
#include "services/equity/EquityResearchModels.h"

#include <QDate>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cmath>

namespace fincept::screens::fingpt {

// ── Forecaster data shapes ───────────────────────────────────────────────────

struct NewsItem {
    QDateTime published_utc;  // EVENT stamp — a real instant (BarTime.h)
    QString headline;
    QString summary;
};

/// One week of the lookback window: the price move between two session closes
/// and the news that landed in between.
struct WeekSlice {
    QDate start;        // session date of the anchor close (exchange calendar)
    QDate end;
    double start_close = 0.0;
    double end_close = 0.0;
    QVector<NewsItem> news;
};

// ── Week slicing (port of app.py get_stock_data) ─────────────────────────────
//
// steps[i] = curday − 7·(n_weeks − i); each step anchors on the FIRST session
// with date ≥ step, and the final anchor is the last candle. Candle timestamps
// are BAR stamps, decoded with Candle::date() only.
//
// Returns an empty vector when the candles cannot cover the window (no session
// on/after the oldest step, or fewer than two distinct anchors) — the caller
// must surface that, not invent a window.
inline QVector<WeekSlice> make_week_slices(const QVector<services::equity::Candle>& candles,
                                           const QDate& curday, int n_weeks) {
    QVector<WeekSlice> out;
    if (candles.isEmpty() || n_weeks < 1 || !curday.isValid())
        return out;

    QVector<QDate> anchor_dates;
    QVector<double> anchor_closes;
    for (int n = n_weeks; n >= 1; --n) {
        const QDate step = curday.addDays(-7 * n);
        qsizetype idx = -1;
        for (qsizetype i = 0; i < candles.size(); ++i) {
            if (candles[i].date() >= step) { idx = i; break; }
        }
        if (idx < 0)
            return {};  // history ends before this step — cannot cover the window
        anchor_dates.append(candles[idx].date());
        anchor_closes.append(candles[idx].close);
    }
    anchor_dates.append(candles.last().date());
    anchor_closes.append(candles.last().close);

    // Every requested week must be a real week. When history starts after the
    // oldest step (recent IPO, short series), consecutive steps anchor on the
    // SAME first session — proceeding would silently shrink a 3-week request
    // into a day or two and present it as the asked-for window. Fail instead;
    // the caller tells the user to lower the lookback.
    for (int i = 0; i + 1 < anchor_dates.size(); ++i)
        if (anchor_dates[i] >= anchor_dates[i + 1])
            return {};

    for (int i = 0; i + 1 < anchor_dates.size(); ++i) {
        WeekSlice w;
        w.start = anchor_dates[i];
        w.end = anchor_dates[i + 1];
        w.start_close = anchor_closes[i];
        w.end_close = anchor_closes[i + 1];
        out.append(w);
    }
    return out;
}

/// Assign news items to week slices. A publication stamp is an EVENT instant;
/// the slice boundaries are session dates — the item lands on the ET calendar
/// day of its instant (core/util/BarTime.h, market_date_et).
///
/// Slice i owns (start, end]; the first slice also owns its own start date.
/// Items newer than the last session close but not after `curday` (today's
/// news, published after the close) belong to the final slice.
inline void bucket_news(QVector<WeekSlice>& slices, const QVector<NewsItem>& items,
                        const QDate& curday) {
    if (slices.isEmpty())
        return;
    for (const NewsItem& it : items) {
        if (!it.published_utc.isValid())
            continue;
        const QDate d = core::bartime::market_date_et(it.published_utc);
        for (int i = 0; i < slices.size(); ++i) {
            const bool is_first = (i == 0);
            const bool is_last = (i == slices.size() - 1);
            const QDate hi = is_last ? std::max(slices[i].end, curday) : slices[i].end;
            const bool after_start = is_first ? (d >= slices[i].start) : (d > slices[i].start);
            if (after_start && d <= hi) {
                slices[i].news.append(it);
                break;
            }
        }
    }
    for (WeekSlice& w : slices)
        std::sort(w.news.begin(), w.news.end(),
                  [](const NewsItem& a, const NewsItem& b) { return a.published_utc < b.published_utc; });
}

/// Deterministic stand-in for upstream's sorted(random.sample(range(n), k)):
/// k indices evenly spaced across [0, n), in order. k ≥ n returns all of them.
inline QVector<int> sample_indices(int n, int k) {
    QVector<int> out;
    if (n <= 0 || k <= 0)
        return out;
    if (k >= n) {
        for (int i = 0; i < n; ++i) out.append(i);
        return out;
    }
    if (k == 1) { out.append(n - 1); return out; }  // one slot → the freshest item
    int prev = -1;
    for (int j = 0; j < k; ++j) {
        int idx = static_cast<int>(std::llround(static_cast<double>(j) * (n - 1) / (k - 1)));
        if (idx <= prev) idx = prev + 1;  // rounding collisions — keep strictly increasing
        prev = idx;
        out.append(idx);
    }
    return out;
}

// ── Prompt text blocks ───────────────────────────────────────────────────────

/// FinGPT-Forecaster's system prompt, verbatim (app.py SYSTEM_PROMPT).
inline QString forecaster_system_prompt() {
    return QStringLiteral(
        "You are a seasoned stock market analyst. Your task is to list the positive "
        "developments and potential concerns for companies based on relevant news and "
        "basic financials from the past weeks, then provide an analysis and prediction "
        "for the companies' stock price movement for the upcoming week. "
        "Your answer format should be as follows:\n\n[Positive Developments]:\n1. ...\n\n"
        "[Potential Concerns]:\n1. ...\n\n[Prediction & Analysis]\nPrediction: ...\nAnalysis: ...");
}

/// Large count for prompt text: "3.45 trillion" — readable by the model, no
/// locale formatting.
inline QString prompt_count(double v) {
    const double a = std::fabs(v);
    if (a >= 1e12) return QStringLiteral("%1 trillion").arg(QString::number(v / 1e12, 'f', 2));
    if (a >= 1e9)  return QStringLiteral("%1 billion").arg(QString::number(v / 1e9, 'f', 2));
    if (a >= 1e6)  return QStringLiteral("%1 million").arg(QString::number(v / 1e6, 'f', 2));
    return QString::number(v, 'f', 2);
}

/// Same, with a currency unit: "3.45 trillion USD".
inline QString prompt_money(double v, const QString& currency) {
    return prompt_count(v) + QLatin1Char(' ')
        + (currency.isEmpty() ? QStringLiteral("USD") : currency);
}

/// FinGPT's "[Company Introduction]" template, with each sentence gated on the
/// fields it needs actually existing (see header comment).
inline QString company_intro(const QString& symbol, const services::equity::StockInfo& info) {
    const QString name = info.company_name.isEmpty() ? symbol : info.company_name;
    const QString industry = !info.industry.isEmpty() ? info.industry : info.sector;

    QStringList parts;
    parts << QStringLiteral("[Company Introduction]:\n");
    if (!industry.isEmpty())
        parts << QStringLiteral("%1 is a leading entity in the %2 sector. The company has "
                                "established its reputation as one of the key players in the market.")
                     .arg(name, industry);
    else
        parts << QStringLiteral("%1 is a publicly traded company.").arg(name);
    if (std::isfinite(info.market_cap) && info.market_cap > 0) {
        QString s = QStringLiteral("As of today, %1 has a market capitalization of %2")
                        .arg(name, prompt_money(info.market_cap, info.currency));
        if (std::isfinite(info.shares_outstanding) && info.shares_outstanding > 0)
            s += QStringLiteral(", with %1 shares outstanding")
                     .arg(prompt_count(info.shares_outstanding));
        s += QStringLiteral(".");
        parts << s;
    }
    {
        QStringList t;
        if (!info.country.isEmpty())
            t << QStringLiteral("%1 operates primarily in %2").arg(name, info.country);
        QString trade = QStringLiteral("trading under the ticker %1").arg(symbol);
        if (!info.exchange.isEmpty())
            trade += QStringLiteral(" on the %1").arg(info.exchange);
        t << (t.isEmpty() ? name + QStringLiteral(" is ") + trade : trade);
        parts << t.join(QStringLiteral(", ")) + QStringLiteral(".");
    }
    return parts.join(QStringLiteral("\n")) + QStringLiteral("\n");
}

/// One week's block: price move head + up to `max_news` news items, FinGPT
/// wording. An empty week carries upstream's exact "No relative news reported."
inline QString week_block(const QString& symbol, const WeekSlice& w, int max_news = 5) {
    const char* term = (w.end_close > w.start_close)   ? "increased"
                       : (w.end_close < w.start_close) ? "decreased"
                                                       : "stayed unchanged";
    QString head = QStringLiteral("From %1 to %2, %3's stock price %4 from %5 to %6. "
                                  "Company news during this period are listed below:\n\n")
                       .arg(w.start.toString(Qt::ISODate), w.end.toString(Qt::ISODate), symbol,
                            QString::fromLatin1(term), QString::number(w.start_close, 'f', 2),
                            QString::number(w.end_close, 'f', 2));
    QStringList items;
    const QVector<int> picks = sample_indices(static_cast<int>(w.news.size()), max_news);
    for (int idx : picks) {
        const NewsItem& n = w.news[idx];
        items << QStringLiteral("[Headline]: %1\n[Summary]: %2\n")
                     .arg(n.headline, n.summary.isEmpty() ? QStringLiteral("(no summary)") : n.summary);
    }
    if (items.isEmpty())
        return head + QStringLiteral("No relative news reported.");
    return head + items.join(QStringLiteral("\n"));
}

/// The "[Basic Financials]" block from StockInfo. finterm has no per-quarter
/// finnhub metric dump, so this carries the fundamentals the terminal already
/// trusts, NaN-gated line by line; with nothing available it degrades to
/// upstream's exact "No basic financial reported." sentence.
inline QString basics_block(const QString& symbol, const services::equity::StockInfo& info) {
    QStringList lines;
    const auto num = [&lines](const char* label, double v, int dp) {
        if (std::isfinite(v))
            lines << QStringLiteral("%1: %2").arg(QLatin1String(label), QString::number(v, 'f', dp));
    };
    const auto pct = [&lines](const char* label, double v) {
        if (std::isfinite(v))
            lines << QStringLiteral("%1: %2%").arg(QLatin1String(label), QString::number(v * 100.0, 'f', 1));
    };
    const auto money = [&lines, &info](const char* label, double v) {
        if (std::isfinite(v))
            lines << QStringLiteral("%1: %2").arg(QLatin1String(label), prompt_money(v, info.currency));
    };
    num("peTTM", info.pe_ratio, 2);
    num("peForward", info.forward_pe, 2);
    num("pegRatio", info.peg_ratio, 2);
    num("priceToBook", info.price_to_book, 2);
    num("evToEbitda", info.ev_to_ebitda, 2);
    money("revenueTTM", info.total_revenue);
    pct("revenueGrowthYoY", info.revenue_growth);
    pct("earningsGrowthYoY", info.earnings_growth);
    pct("grossMargin", info.gross_margins);
    pct("operatingMargin", info.operating_margins);
    pct("netMargin", info.profit_margins);
    pct("roe", info.roe);
    pct("roa", info.roa);
    money("freeCashFlow", info.free_cashflow);
    money("totalCash", info.total_cash);
    money("totalDebt", info.total_debt);
    num("beta", info.beta, 2);
    pct("dividendYield", info.dividend_yield);

    if (lines.isEmpty())
        return QStringLiteral("[Basic Financials]:\n\nNo basic financial reported.");
    return QStringLiteral("Some recent basic financials of %1 are presented below:\n\n"
                          "[Basic Financials]:\n\n%2")
        .arg(symbol, lines.join(QStringLiteral("\n")));
}

/// FinGPT's inference-time closing instruction, verbatim (app.py
/// get_all_prompts_online).
inline QString closing_instruction(const QString& symbol, const QDate& curday) {
    const QString period = QStringLiteral("%1 to %2").arg(curday.toString(Qt::ISODate),
                                                          curday.addDays(7).toString(Qt::ISODate));
    return QStringLiteral(
               "\n\nBased on all the information before %1, let's first analyze the positive "
               "developments and potential concerns for %2. Come up with 2-4 most important "
               "factors respectively and keep them concise. Most factors should be inferred from "
               "company related news. Then make your prediction of the %2 stock price movement "
               "for next week (%3). Provide a summary analysis to support your prediction.")
        .arg(curday.toString(Qt::ISODate), symbol, period);
}

/// The full user prompt: intro + week blocks + basics + closing instruction.
/// `slices` must already carry their news (bucket_news).
inline QString forecaster_user_prompt(const QString& symbol, const services::equity::StockInfo& info,
                                      const QVector<WeekSlice>& slices, const QDate& curday,
                                      bool with_basics) {
    QStringList parts;
    parts << company_intro(symbol, info);
    for (const WeekSlice& w : slices)
        parts << week_block(symbol, w);
    parts << (with_basics ? basics_block(symbol, info)
                          : QStringLiteral("[Basic Financials]:\n\nNo basic financial reported."));
    return parts.join(QStringLiteral("\n")) + closing_instruction(symbol, curday);
}

// ── Sentiment (FinGPT_Benchmark instruction formats) ─────────────────────────

/// The canonical FinGPT sentiment instruction (bench FPB/FiQA/NWGI wording).
/// `text_type` is "news" or "tweet".
inline QString sentiment_instruction_canonical(const QString& text_type = QStringLiteral("news")) {
    return QStringLiteral("What is the sentiment of this %1? Please choose an answer from "
                          "{negative/neutral/positive}.")
        .arg(text_type);
}

/// The 5 multi-template voting instructions (sentiment_templates.txt), each
/// suffixed with an Options line as in the benchmark harness — ours includes
/// neutral (see header comment).
inline QStringList sentiment_vote_instructions(const QString& text_type = QStringLiteral("news")) {
    static const char* kTemplates[] = {
        "What is the sentiment of the input %1 from financial perspective?",
        "Assign a sentiment category to this %1 related to finance.",
        "Categorize the input %1's emotional tone into one of three groups.",
        "Determine the sentiment expressed in the %1 from financial perspective.",
        "Characterize the %1's sentiment using the following options.",
    };
    QStringList out;
    for (const char* t : kTemplates)
        out << QString::fromLatin1(t).arg(text_type)
                   + QStringLiteral("\nOptions: positive, negative, neutral");
    return out;
}

/// FinGPT's "Instruction:/Input:/Answer: " completion frame (format_example).
inline QString sentiment_frame(const QString& instruction, const QString& input) {
    return QStringLiteral("Instruction: %1\nInput: %2\nAnswer: ").arg(instruction, input);
}

/// Substring label extraction — port of the benchmark's change_target():
/// positive beats negative when both appear, anything else is neutral.
inline QString parse_sentiment_label(const QString& output) {
    const QString s = output.toLower();
    if (s.contains(QStringLiteral("positive"))) return QStringLiteral("positive");
    if (s.contains(QStringLiteral("negative"))) return QStringLiteral("negative");
    return QStringLiteral("neutral");
}

/// Majority vote across template runs — port of vote_output(): positive vs
/// negative head-to-head, ties (and all-neutral) resolve neutral.
inline QString vote_sentiment(const QStringList& labels) {
    int pos = 0, neg = 0;
    for (const QString& l : labels) {
        if (l == QStringLiteral("positive")) ++pos;
        else if (l == QStringLiteral("negative")) ++neg;
    }
    if (pos > neg) return QStringLiteral("positive");
    if (neg > pos) return QStringLiteral("negative");
    return QStringLiteral("neutral");
}

} // namespace fincept::screens::fingpt

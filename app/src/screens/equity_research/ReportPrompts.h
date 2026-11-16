// src/screens/equity_research/ReportPrompts.h
#pragma once
// The two language-model requests behind the ER Report's prose, and the
// parsing of what comes back. Pure, so the prompts are unit-tested.
//
//   business  reads the company's own filings (10-K Business, Risk Factors,
//             Management's Discussion; the latest 10-Q) and recent headlines,
//             and sorts what they say into the report's topics.
//   summary   reads the figures the report already computed and writes the
//             executive summary, the two cases, what to watch — and a
//             direction, which is a word and never a number.
//
// A model may quote a figure, never originate one: every number it can use is
// in the prompt, and LlmService removes any that is not (NumberGrounding.h).
// Neither request asks for a price target, a probability or a percentage.

#include "screens/equity_research/ReportModel.h"

#include <QJsonDocument>
#include <QString>
#include <QStringList>

namespace finterm::screens::report {

/// "Flat" in the three-month reading: within this percent of the price at the
/// call. Must equal ai_forecast::kReportFlatBandPct, which grades it (pinned
/// by test_report_model).
inline constexpr double kOutlookFlatBandPct = 5.0;

struct Headline {
    QString title, publisher, date;
};

/// Whether a headline is about this company. The vendor's per-symbol feed
/// mixes in market-wide stories; a report on Walmart is not the place for
/// them. A headline counts when it names the ticker or the company's own
/// name. A company better known by another name than either (Alphabet's
/// "Google" is caught; others may not be) can lose a story — the price of
/// not listing other companies' news as its own.
inline bool headline_mentions(const QString& title, const QString& symbol, const QString& company) {
    auto word_at = [&title](qsizetype at, qsizetype len) {
        const bool left = at == 0 || !title[at - 1].isLetterOrNumber();
        const qsizetype end = at + len;
        return left && (end >= title.size() || !title[end].isLetterOrNumber());
    };
    // The ticker, as tickers are written: upper case, a word of its own. A
    // short one, or one that is also an English word (NOW, ALL, ON), counts
    // only where it is marked as a ticker — "(NOW)", "$NOW", "NYSE: NOW".
    static const QStringList words = {"NOW", "ALL", "ON", "IT", "ARE", "FOR", "ONE", "OUT", "BIG", "CAN", "HAS",
                                      "SO",  "GO",  "BE", "AI", "DAY", "KEY", "LOW", "NEW", "OLD", "RUN", "SEE",
                                      "TWO", "WELL", "REAL", "FAST", "CASH", "GOLD", "LIFE", "LOVE", "OPEN", "PLAY",
                                      "SAVE", "TRUE", "COST", "EVER", "NICE", "RARE", "SAFE", "EAT",  "FUN",  "HE"};
    if (!symbol.isEmpty()) {
        const bool needs_marking = symbol.size() <= 2 || words.contains(symbol.toUpper());
        for (qsizetype at = 0; (at = title.indexOf(symbol.toUpper(), at, Qt::CaseSensitive)) >= 0; at += symbol.size()) {
            if (!word_at(at, symbol.size()))
                continue;
            const QChar before = at > 0 ? title[at - 1] : QChar();
            const bool marked = before == QLatin1Char('(') || before == QLatin1Char('$') ||
                                (at >= 2 && title[at - 1] == QLatin1Char(' ') && title[at - 2] == QLatin1Char(':'));
            if (!needs_marking || marked)
                return true;
        }
    }
    // The company's own name: its first word (the second after "The"), as a
    // capitalised word of its own — "Target" the retailer, not a "price target".
    QString name = company.section(QLatin1Char(' '), 0, 0);
    if (name.compare(QLatin1String("the"), Qt::CaseInsensitive) == 0)
        name = company.section(QLatin1Char(' '), 1, 1);
    name.remove(QLatin1Char(',')).remove(QLatin1Char('.'));
    // A short or generic first word ("First", "General", "American") would match anything.
    static const QStringList generic = {"first", "general", "american", "national", "united", "international",
                                        "global", "new", "bank", "group", "home"};
    if (name.size() >= 4 && !generic.contains(name.toLower())) {
        const QString written = name.left(1).toUpper() + name.mid(1);
        for (qsizetype at = 0; (at = title.indexOf(written, at, Qt::CaseSensitive)) >= 0; at += written.size()) {
            const bool left = at == 0 || !title[at - 1].isLetterOrNumber();
            const qsizetype end = at + written.size();
            // "Walmart's" and "Walmart" both count; "Targeted" does not.
            const bool right = end >= title.size() || !title[end].isLetter() ||
                               (title[end] == QLatin1Char('s') && (end + 1 >= title.size() || !title[end + 1].isLetter()));
            if (left && right)
                return true;
        }
    }
    // A name the ticker abbreviates ("Google" for GOOGL): a capitalised word
    // that begins with a long ticker. Five letters, so META does not claim "Metals".
    if (symbol.size() >= 5) {
        const QString stem = symbol.left(1).toUpper() + symbol.mid(1).toLower();
        for (qsizetype at = 0; (at = title.indexOf(stem, at, Qt::CaseSensitive)) >= 0; at += stem.size())
            if (at == 0 || !title[at - 1].isLetterOrNumber())
                return true;
    }
    return false;
}

/// The headlines that are about the company; all of them when none can be
/// told apart (a name too generic to match on).
inline QVector<Headline> relevant_headlines(const QVector<Headline>& all, const QString& symbol, const QString& company) {
    QVector<Headline> out;
    for (const auto& h : all)
        if (headline_mentions(h.title, symbol, company))
            out.append(h);
    return out.isEmpty() ? all : out;
}

/// The DATA wrapper both prompts use; the model is told nothing outside it exists.
inline QString data_fence(const QString& body) {
    return QStringLiteral("<<<DATA>>>\n%1\n<<<END>>>").arg(body.trimmed());
}

inline QString headlines_block(const QVector<Headline>& news, int limit = 14) {
    QStringList lines;
    for (int i = 0; i < news.size() && i < limit; ++i)
        lines << QStringLiteral("- %1 (%2%3)").arg(news[i].title.simplified(), news[i].publisher,
                                                   news[i].date.isEmpty() ? QString() : QStringLiteral(", ") + news[i].date.left(10));
    return lines.join(QLatin1Char('\n'));
}

/// `text` up to `limit` characters, ending on a paragraph where one is near.
inline QString excerpt(const QString& text, int limit) {
    if (text.size() <= limit)
        return text;
    const QString head = text.left(limit);
    int stop = head.lastIndexOf(QStringLiteral("\n\n"));
    if (stop < limit * 6 / 10)
        stop = head.lastIndexOf(QStringLiteral(". ")) + 1;
    return (stop < limit * 6 / 10 ? head : head.left(stop)).trimmed();
}

// How much of each filing section goes to the model. A local model reads a
// prompt at a few hundred tokens a second and the request is cut off at two
// minutes, so the whole business prompt is held near five thousand tokens —
// the opening of each section, which is where a filing puts what matters.
inline constexpr int kPromptBusinessChars = 6500;
inline constexpr int kPromptQuarterChars = 4500;
inline constexpr int kPromptAnnualMdnaChars = 3000;
inline constexpr int kPromptRiskChars = 3500;

/// Revenue by segment / product / geography and the disclosed customers, as
/// lines the model may quote from.
inline QString breakdown_block(const Filings& f) {
    QStringList lines;
    for (const auto& b : f.breakdowns) {
        QStringList rows;
        for (const auto& r : b.rows)
            rows << QStringLiteral("%1 %2%3").arg(r.label, compact(r.value),
                                                 ok(r.growth_pct()) ? QStringLiteral(" (%1 on the year before)").arg(sp1(r.growth_pct()))
                                                                    : QString());
        lines << QStringLiteral("%1, year ended %2: %3").arg(b.title(), b.period, rows.join(QStringLiteral("; ")));
    }
    for (const auto& c : f.customers)
        lines << QStringLiteral("%1 accounted for %2 of %3 revenue").arg(c.label, p1(c.pct), c.scope.isEmpty() ? QStringLiteral("total") : c.scope);
    return lines.join(QLatin1Char('\n'));
}

inline QString business_prompt(const QString& symbol, const QString& company, const QString& profile,
                               const Filings& filings, const QVector<Headline>& news) {
    QStringList parts;
    if (!profile.trimmed().isEmpty())
        parts << QStringLiteral("## Company profile\n") + profile.trimmed();
    const QString cuts = breakdown_block(filings);
    if (!cuts.isEmpty())
        parts << QStringLiteral("## Revenue as the annual report breaks it down (%1)\n%2").arg(filings.breakdown_source, cuts);
    auto add = [&parts](const QString& heading, const Filing& f, const FilingSection& s, int limit) {
        if (!s.text.isEmpty())
            parts << QStringLiteral("## %1 — %2\n%3").arg(heading, f.cite(), excerpt(s.text, limit));
    };
    add(QStringLiteral("Business (Item 1)"), filings.annual, filings.annual.business, kPromptBusinessChars);
    add(QStringLiteral("Management's discussion, latest quarter"), filings.quarterly, filings.quarterly.mdna,
        kPromptQuarterChars);
    // The year's discussion only when there is no quarter's: they overlap.
    if (filings.quarterly.mdna.text.isEmpty())
        add(QStringLiteral("Management's discussion, fiscal year"), filings.annual, filings.annual.mdna,
            kPromptAnnualMdnaChars);
    add(QStringLiteral("Risk factors (Item 1A)"), filings.annual, filings.annual.risk_factors, kPromptRiskChars);
    const QString heads = headlines_block(news, 10);
    if (!heads.isEmpty())
        parts << QStringLiteral("## Recent headlines\n") + heads;

    return QStringLiteral(
               "You are preparing the business section of an investor report on %1 (%2).\n"
               "Use ONLY the material between <<<DATA>>> and <<<END>>>. It is the company's own filings and recent "
               "headlines. Do not add facts from memory. Quote a figure only exactly as it appears in the material; "
               "never estimate, round differently or compute a new one. If the material does not cover a topic, "
               "return an empty string or empty list for it — do not fill the gap.\n\n"
               "%3\n\n"
               "Reply with ONE JSON object and nothing else, with exactly these keys:\n"
               "{\n"
               "  \"business_model\": \"2-3 plain sentences: what the company sells, to whom, and how it earns money\",\n"
               "  \"segments\": [\"one line per business segment or product line: its name, then what is driving it\"],\n"
               "  \"customers\": \"who the customers are; any concentration or dependence the filings disclose\",\n"
               "  \"competition\": \"the competitors the filings name and what the company says it competes on\",\n"
               "  \"new_technology\": [\"new products, platforms or technology described as recent or upcoming\"],\n"
               "  \"new_business\": [\"new markets, partnerships, acquisitions or business lines\"],\n"
               "  \"investment\": \"where the company says it is putting capital: R&D, capacity, acquisitions, buybacks\",\n"
               "  \"advantages\": [\"up to 5 durable advantages the material supports\"],\n"
               "  \"risks\": [\"up to 6 of the most material risks, each a short sentence in plain words\"],\n"
               "  \"recent_developments\": [\"up to 5 developments from the latest quarter and the headlines\"]\n"
               "}\n"
               "Each list item is one sentence. Plain words, no marketing language, no markdown.")
        .arg(company.isEmpty() ? symbol : company, symbol, data_fence(parts.join(QStringLiteral("\n\n"))));
}

inline QString summary_prompt(const QString& symbol, const QString& company, const QString& figures,
                              const QVector<Finding>& findings, const QVector<Headline>& news) {
    QStringList f;
    for (const auto& x : findings)
        f << QStringLiteral("- [%1] %2: %3 %4")
                 .arg(x.tone == Tone::Strength ? QStringLiteral("strength") : x.tone == Tone::Risk ? QStringLiteral("risk")
                                                                                                    : QStringLiteral("note"),
                      x.area, x.title, x.detail);
    QStringList parts;
    parts << QStringLiteral("## Figures\n") + figures.trimmed();
    if (!f.isEmpty())
        parts << QStringLiteral("## Findings (computed by fixed rules)\n") + f.join(QLatin1Char('\n'));
    const QString heads = headlines_block(news);
    if (!heads.isEmpty())
        parts << QStringLiteral("## Recent headlines\n") + heads;

    return QStringLiteral(
               "You are writing the executive summary of a quarterly investor report on %1 (%2).\n"
               "Use ONLY the material between <<<DATA>>> and <<<END>>>. Quote a figure only exactly as it appears "
               "there; never estimate or compute a new one. Do not give a price target, a probability, a score or "
               "an expected return — the direction is a word.\n\n"
               "%3\n\n"
               "Reply with ONE JSON object and nothing else, with exactly these keys:\n"
               "{\n"
               "  \"summary\": \"4-6 plain sentences an investor can read in half a minute: how the business is "
               "doing, what changed this quarter, how the stock is priced\",\n"
               "  \"bull_case\": [\"3 short sentences: the strongest reasons to own it, each tied to a figure above\"],\n"
               "  \"bear_case\": [\"3 short sentences: the strongest reasons for caution, each tied to a figure above\"],\n"
               "  \"watch\": [\"3 things to watch in the next report, each one sentence\"],\n"
               "  \"direction\": \"up\" or \"flat\" or \"down\" — your reading of the share price three months from "
               "now against today's; \"flat\" means within %4 either way,\n"
               "  \"rationale\": \"one sentence saying which of the figures above decides the direction\"\n"
               "}\n"
               "Plain words, no markdown.")
        .arg(company.isEmpty() ? symbol : company, symbol, data_fence(parts.join(QStringLiteral("\n\n"))),
             QStringLiteral("%1 percent").arg(kOutlookFlatBandPct, 0, 'f', 0));
}

// ── replies ─────────────────────────────────────────────────────────────────

/// The answer in a reply: the LAST complete JSON object that carries `key`.
/// A model may think aloud before answering, drafting objects on the way, and
/// may wrap the answer in prose or a code fence; the final object is the one
/// it meant.
inline QJsonObject reply_object(const QString& reply, const char* key) {
    QJsonObject answer;
    int depth = 0, start = -1;
    bool in_string = false, escaped = false;
    for (int i = 0; i < reply.size(); ++i) {
        const QChar c = reply[i];
        if (in_string) {
            if (escaped)
                escaped = false;
            else if (c == QLatin1Char('\\'))
                escaped = true;
            else if (c == QLatin1Char('"'))
                in_string = false;
            continue;
        }
        if (c == QLatin1Char('"'))
            in_string = true;
        else if (c == QLatin1Char('{')) {
            if (depth++ == 0)
                start = i;
        } else if (c == QLatin1Char('}') && depth > 0 && --depth == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(reply.mid(start, i - start + 1).toUtf8());
            if (doc.isObject() && doc.object().contains(QLatin1String(key)))
                answer = doc.object();
            start = -1;
        }
    }
    return answer;
}

inline QString text_field(const QJsonObject& o, const char* key) {
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isString())
        return v.toString().simplified();
    if (v.isArray()) { // a model that returned a list where a paragraph was asked for
        QStringList parts;
        for (const auto& x : v.toArray())
            if (x.isString() && !x.toString().simplified().isEmpty())
                parts << x.toString().simplified();
        return parts.join(QLatin1Char(' '));
    }
    return {};
}

inline QStringList list_field(const QJsonObject& o, const char* key, int limit) {
    QStringList out;
    const QJsonValue v = o.value(QLatin1String(key));
    if (v.isArray()) {
        for (const auto& x : v.toArray()) {
            const QString s = x.isString() ? x.toString().simplified() : QString();
            if (!s.isEmpty() && out.size() < limit)
                out << s;
        }
    } else if (v.isString() && !v.toString().simplified().isEmpty()) {
        out << v.toString().simplified();
    }
    return out;
}

struct BusinessNarrative {
    bool valid = false;
    QString business_model, customers, competition, investment;
    QStringList segments, new_technology, new_business, advantages, risks, recent_developments;
};

inline BusinessNarrative parse_business(const QString& reply) {
    BusinessNarrative n;
    const QJsonObject o = reply_object(reply, "business_model");
    if (o.isEmpty())
        return n;
    n.business_model = text_field(o, "business_model");
    n.customers = text_field(o, "customers");
    n.competition = text_field(o, "competition");
    n.investment = text_field(o, "investment");
    n.segments = list_field(o, "segments", 8);
    n.new_technology = list_field(o, "new_technology", 6);
    n.new_business = list_field(o, "new_business", 6);
    n.advantages = list_field(o, "advantages", 5);
    n.risks = list_field(o, "risks", 6);
    n.recent_developments = list_field(o, "recent_developments", 5);
    n.valid = !n.business_model.isEmpty() || !n.segments.isEmpty() || !n.risks.isEmpty() || !n.advantages.isEmpty();
    return n;
}

struct SummaryNarrative {
    bool valid = false;
    QString summary, rationale;
    QStringList bull_case, bear_case, watch;
    QString direction; ///< "up" | "flat" | "down", or empty when the model gave anything else
};

inline SummaryNarrative parse_summary(const QString& reply) {
    SummaryNarrative n;
    const QJsonObject o = reply_object(reply, "summary");
    if (o.isEmpty())
        return n;
    n.summary = text_field(o, "summary");
    n.rationale = text_field(o, "rationale");
    n.bull_case = list_field(o, "bull_case", 4);
    n.bear_case = list_field(o, "bear_case", 4);
    n.watch = list_field(o, "watch", 4);
    const QString d = text_field(o, "direction").toLower();
    // Anything but the three words is not a call: nothing is inferred from it.
    if (d == QLatin1String("up") || d == QLatin1String("flat") || d == QLatin1String("down"))
        n.direction = d;
    n.valid = !n.summary.isEmpty();
    return n;
}

} // namespace finterm::screens::report

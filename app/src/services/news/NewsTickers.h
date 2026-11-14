#pragma once
// NewsTickers.h — which tickers a news article is about.
//
// The tagger used to take every all-caps 2-5 letter word as a ticker, so the
// feed showed $COO, $CEO, $VP, $NSW, $UN, $EU, $AI, $US, $FAA, $EPS, $GAAP,
// $ETF … — job titles, agencies, places and accounting terms, rendered with a
// "$" the source never wrote. An all-caps word is not evidence of a ticker
// (COO is both The Cooper Companies and every chief operating officer).
//
// A symbol is tagged only on evidence:
//   1. an explicit cashtag in the source text ("$NVDA"), or
//   2. exchange context ("(NYSE: COO)", "Nasdaq:AAPL"), or
//   3. a short list of large, unambiguous symbols written bare ("NVDA").
// Paths 1 and 3 also pass a stoplist of acronyms; exchange context is
// definitive and skips it ("(NYSE: AI)" is C3.ai).
//
// Header-only, Qt-Core-only: used by NewsService and by its test.

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace finterm::news {

/// Acronyms, titles, agencies, places, currencies and accounting terms that
/// headlines write in capitals and that are never meant as a stock reference.
inline const QSet<QString>& ticker_stoplist() {
    static const QSet<QString> k = {
        // titles / roles
        "CEO", "CFO", "COO", "CTO", "CIO", "CMO", "CRO", "CSO", "EVP", "SVP", "VP", "MP", "MEP", "PM", "GOP", "DEM",
        // agencies, bodies, places, countries
        "UN", "EU", "US", "USA", "UK", "UAE", "NSW", "NYC", "DC", "LA", "FAA", "FDA", "SEC", "FTC", "DOJ", "FBI",
        "CIA", "IRS", "EPA", "DOE", "DOD", "NATO", "IMF", "ECB", "BOE", "BOJ", "PBOC", "FED", "FOMC", "OPEC", "WHO",
        "WTO", "NHS", "NASA", "FCC", "CFTC", "FINRA", "OCC", "FDIC", "CBO", "BLS", "BEA", "ISM", "EIA", "IEA",
        "G7", "G20", "APAC", "EMEA", "ASEAN", "BRICS",
        // currencies
        "USD", "EUR", "GBP", "JPY", "CNY", "RMB", "CHF", "CAD", "AUD", "NZD", "HKD", "SGD", "INR", "KRW",
        // markets / instruments / accounting
        "AI", "EPS", "GAAP", "ETF", "ETFS", "IPO", "IPOS", "SPAC", "GDP", "CPI", "PPI", "PCE", "PMI", "ESG", "M&A",
        "LBO", "NAV", "AUM", "YOY", "QOQ", "EBIT", "EBITDA", "ROE", "ROI", "API", "ADR", "ADRS", "OTC", "NYSE",
        "NASDAQ", "TSX", "LSE", "ASX", "CBOE", "AMEX", "REIT", "REITS", "LNG", "EV", "EVS", "OEM", "IT", "TV", "PR",
        "Q1", "Q2", "Q3", "Q4", "H1", "H2", "FY", "YTD", "ATH", "IRA", "LLC", "INC", "PLC", "LTD", "CORP", "CO",
        // common capitalised words in headlines
        "THE", "FOR", "AND", "BUT", "NOT", "NEW", "ALL", "ONE", "TWO", "TOP", "BIG", "NOW", "WHY", "HOW", "WHAT",
        "LIVE", "UPDATE", "BREAKING", "WATCH", "OK", "TBA", "TBD", "FAQ", "PDF", "AM", "ET", "PT", "GMT", "UTC",
    };
    return k;
}

/// Large, frequently-headlined symbols that are not English words or common
/// acronyms, so a bare all-caps mention is a fair signal on its own.
inline const QSet<QString>& bare_known_symbols() {
    static const QSet<QString> k = {
        "AAPL", "MSFT", "NVDA", "TSLA", "AMZN", "GOOGL", "GOOG", "AVGO", "INTC", "NFLX", "PLTR", "ORCL", "ADBE",
        "QCOM", "SMCI", "MSTR", "PYPL", "BABA", "JPM",  "XOM",  "CVX",  "TSM",  "ASML", "LLY",  "UNH",  "JNJ",
        "PFE",  "MRNA", "NVO",  "SPY",  "QQQ",  "BRK.B", "BRK.A",
    };
    return k;
}

/// Tickers named in `text` (headline + summary, original case), in first-seen
/// order, at most `max_count`.
inline QStringList extract_tickers(const QString& text, int max_count = 5) {
    // $SYM / $BRK.B — not part of a word or a dollar amount ("$5", "US$").
    static const QRegularExpression kCashtag(QStringLiteral("(?<![A-Za-z0-9$])\\$([A-Z]{1,5}(?:\\.[A-Z]{1,2})?)(?![A-Za-z0-9])"));
    // (NYSE: COO), Nasdaq:AAPL, NYSE American: XYZ, TSX: SHOP, OTCQX: ABCD
    static const QRegularExpression kExchange(
        QStringLiteral("\\b(?:NYSE(?:\\s+(?:American|Arca|MKT))?|NASDAQ(?:GS|GM|CM)?|AMEX|CBOE|BATS|OTC(?:QX|QB|"
                       "\\s+Markets|\\s+Pink)?|TSXV?|ASX|LSE)\\s*:\\s*([A-Z]{1,5}(?:\\.[A-Z]{1,2})?)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kBare(QStringLiteral("(?<![A-Za-z0-9$.])([A-Z]{2,5}(?:\\.[A-Z])?)(?![A-Za-z0-9])"));

    struct Hit {
        qsizetype pos;
        QString sym;
    };
    QList<Hit> hits;
    const auto& stop = ticker_stoplist();
    for (auto it = kExchange.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        // The exchange word is case-insensitive; the symbol must be capitals.
        const QString sym = m.captured(1);
        if (sym == sym.toUpper())
            hits.append({m.capturedStart(1), sym});
    }
    for (auto it = kCashtag.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (!stop.contains(m.captured(1)))
            hits.append({m.capturedStart(1), m.captured(1)});
    }
    const auto& known = bare_known_symbols();
    for (auto it = kBare.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (known.contains(m.captured(1)))
            hits.append({m.capturedStart(1), m.captured(1)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.pos < b.pos; });

    QStringList out;
    for (const auto& h : hits) {
        if (out.size() >= max_count)
            break;
        if (!out.contains(h.sym))
            out << h.sym;
    }
    return out;
}

} // namespace finterm::news

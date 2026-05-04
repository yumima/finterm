#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace fincept::knowledge {

struct ExternalLink {
    QString type;     ///< primer, reference, deep_dive, video, tool
    QString source;   ///< Investopedia, SEC, Khan Academy, etc.
    QString title;
    QString url;
};

struct ScreenRef {
    QString screen;   ///< route id (e.g. "markets")
    QString label;    ///< human-readable hint ("Markets · stock detail")
};

/// A "Try in finterm" action — opens a screen, optionally setting the
/// active SymbolContext ticker first so the target screen loads with
/// the right context.
struct ActionRef {
    QString label;    ///< button text ("Open AAPL in Markets")
    QString screen;   ///< route id
    QString ticker;   ///< optional: set this on SymbolContext before navigation
    QString hint;     ///< optional: tooltip / one-line explanation
};

/// A live data point fetched from MarketDataService and shown in the right
/// rail. The metric maps to a field on InfoData/QuoteData.
struct LiveExample {
    QString label;    ///< display label ("AAPL P/E")
    QString ticker;   ///< symbol to fetch
    QString metric;   ///< pe_ratio, forward_pe, price_to_book, dividend_yield, beta,
                      ///<  market_cap, eps, roe, profit_margin, debt_to_equity,
                      ///<  current_ratio, price, change_pct
};

struct Warning {
    QString severity; ///< low, medium, high
    QString text;
};

/// One row in the rail's "Rule of thumb" panel — concise norm or range.
struct RuleOfThumb {
    QString label;    ///< short label ("Mature sector P/E")
    QString value;    ///< value or range ("10–25")
    QString note;     ///< optional one-line note
};

/// One row in the rail's "Common pitfalls" panel — short cautionary item.
/// Body is plain text; renderer wraps with a leading bullet.
struct Pitfall {
    QString text;
};

/// Per-entry calculator definition — gives the rail a small interactive
/// formula widget. `kind` selects the implementation: "ratio" supports
/// simple A / B style metrics (price ÷ EPS → P/E); future kinds can be
/// added without breaking the schema.
struct Calculator {
    QString kind;     ///< "ratio" for now
    QString numerator_label;
    QString denominator_label;
    QString result_label;
    QString result_format; ///< "ratio" (×) or "percent" (%) or "currency"
    double  numerator_default = 0.0;
    double  denominator_default = 0.0;
};

struct Difficulty {
    static constexpr const char* BEGINNER = "beginner";
    static constexpr const char* INTERMEDIATE = "intermediate";
    static constexpr const char* ADVANCED = "advanced";
    static constexpr const char* PRO = "pro";
};

struct KnowledgeEntry {
    QString id;
    QString title;
    QString subtitle;
    QString body_path;
    QString category;
    QString difficulty;
    QStringList tags;
    QStringList aliases;
    QString abbreviation;
    QStringList related;
    QVector<ScreenRef> seen_in;
    QVector<ActionRef> actions;
    QVector<LiveExample> live_examples;
    QVector<RuleOfThumb> rules_of_thumb;
    QVector<Pitfall> pitfalls;
    QVector<QString> peer_tickers;       ///< for "peer comparison" panel; uses live_examples[0].metric
    QString primary_ticker;              ///< for "mini history chart" panel
    QString primary_metric;              ///< default: live_examples[0].metric if present
    Calculator calculator;
    QStringList exposure_tickers;        ///< explicit tickers to flag in user's portfolio
    QString exposure_criterion;          ///< human-readable filter: "P/E > 30"
    QVector<ExternalLink> external_links;
    QVector<Warning> warnings;
    QString updated;
};

struct KnowledgeCategory {
    QString id;
    QString label;
    QString description;
    int order = 0;
    QVector<KnowledgeEntry> entries;
};

} // namespace fincept::knowledge

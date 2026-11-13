#pragma once
// BrokerCsv.h — read a broker's CSV export into portfolio transactions.
//
// Two kinds of file, both exported by every US broker in some shape:
//   - Positions: one row per holding (symbol, quantity, cost basis). Each
//     becomes a single BUY dated "as of" the export, at the average cost — the
//     book is right from that date on; history before it is unknown.
//   - Activity: one row per trade / dividend. Brokers list it newest first;
//     it is put back in time order so a same-day buy replays before its sell.
//     Splits, stock dividends, short sales and dividend reversals are skipped
//     and reported (the ratio or the short leg isn't in the row).
//
// No per-broker templates: column names are matched against synonyms
// (guess_mapping) and the caller shows the guess for the user to correct
// before anything is written. Rows that are not a holding or a supported
// action — totals, cash, pending, options, transfers — are skipped and
// reported, never silently dropped.
//
// Pure functions: no widgets, no DB, no network.

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace fincept::portfolio::csv {

enum class Field {
    Ignore,
    Date,
    Action,
    Symbol,
    Quantity,
    Price,          // per-share trade price (activity) or last price (positions)
    Amount,         // signed cash amount of the row (activity)
    CostBasisTotal, // positions
    AverageCost,    // positions: per-share cost
};

enum class Kind { Positions, Activity };

struct Table {
    QStringList headers;
    QVector<QStringList> rows;
    QVector<int> row_lines;    // 1-based file line of each row (blank rows and multi-line cells counted)
    int header_line = 0;       // 1-based line of the header row in the file
    bool decimal_comma = false; // ';'-separated files write "12,50": comma is the decimal mark
};

/// Parse CSV bytes (RFC 4180 quoting; comma, semicolon or tab, detected).
/// Brokers put account names and dates above the real header, so the header
/// is the first row whose cells look like column names (at least two match
/// a known synonym). Error text when there is no such row.
std::optional<Table> read_table(const QByteArray& bytes, QString* error = nullptr);

/// Column index → field, from header synonyms. Unmatched columns: Ignore.
/// Each field is given to at most one column; to_transactions likewise
/// takes the lowest-numbered column when a caller maps a field twice.
QHash<int, Field> guess_mapping(const QStringList& headers);

/// Activity when an Action column is mapped, Positions otherwise.
Kind guess_kind(const QHash<int, Field>& mapping);

/// "$1,234.56" → 1234.56, "(12.30)" → -12.3, "-5%"/"--"/"n/a"/"" → nullopt.
/// With decimal_comma: "1.234,56" → 1234.56.
std::optional<double> parse_number(QString text, bool decimal_comma = false);

/// MM/dd/yyyy, M/d/yy, yyyy-MM-dd, dd-MMM-yyyy, "MM/dd/yyyy as of MM/dd/yyyy".
std::optional<QDate> parse_date(QString text);

struct ParsedTxn {
    QString date; // ISO yyyy-MM-dd
    QString symbol;
    QString type; // BUY / SELL / DIVIDEND
    double quantity = 0;
    double price = 0;
    QString note;
};

struct ParseResult {
    QVector<ParsedTxn> transactions;
    QStringList skipped; // "line 14: Cash & Cash Investments — not a security"
    QStringList warnings;
};

/// Rows → transactions. `as_of` dates every holding of a Positions file.
ParseResult to_transactions(const Table& table, const QHash<int, Field>& mapping, Kind kind, const QDate& as_of);

QString field_name(Field f);

} // namespace fincept::portfolio::csv
